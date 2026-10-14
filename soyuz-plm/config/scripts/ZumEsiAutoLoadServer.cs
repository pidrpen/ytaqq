/// <summary>
/// Серверная функция «Автозагрузка ЭСИ».
/// Её вызывают автоматическое действие ZumEsiAutoLoad (по расписанию)
/// и команда ZumEsiLoadNow (вручную). Берёт файлы *.csv из папки входящих,
/// находит сборку по имени файла (обозначение) и заполняет её состав (Items)
/// так же, как команда «Сформировать состав на основе Excel».
///
/// Формат файла: кодировка UTF-8, разделитель «;», по строке на позицию:
///   Обозначение;Наименование;Количество
/// Имя файла = обозначение сборки, например «АБВГ.123456.001.csv».
/// После обработки файл переносится в подпапку «Загружено» или «Ошибки»
/// (рядом пишется .log с причиной).
/// </summary>
/// <param name="session">Пользовательская сессия (null при запуске по расписанию)</param>
/// <param name="inputParams">Не используется</param>
/// <returns>Короткий отчёт по обработанным файлам</returns>
public override Object Invoke(UserSession session, Object inputParams)
{
    string inbox = InboxFolder;
    if (!System.IO.Directory.Exists(inbox))
    {
        Service.WriteToServerLog($"Автозагрузка ЭСИ: папка {inbox} не найдена");
        return $"Папка {inbox} не найдена на сервере";
    }
    var summary = new System.Text.StringBuilder();

    string doneDir = System.IO.Path.Combine(inbox, "Загружено");
    string errorDir = System.IO.Path.Combine(inbox, "Ошибки");
    System.IO.Directory.CreateDirectory(doneDir);
    System.IO.Directory.CreateDirectory(errorDir);

    foreach (var file in System.IO.Directory.GetFiles(inbox, "*.csv"))
    {
        string stamp = DateTime.Now.ToString("yyyy-MM-dd_HH-mm-ss");
        string target = System.IO.Path.GetFileNameWithoutExtension(file) + "_" + stamp + ".csv";
        try
        {
            string log = LoadFile(file);
            System.IO.File.Move(file, System.IO.Path.Combine(doneDir, target));
            System.IO.File.WriteAllText(System.IO.Path.Combine(doneDir, target + ".log"), log);
            summary.AppendLine($"{System.IO.Path.GetFileName(file)}: загружено");
        }
        catch (Exception ex)
        {
            Service.WriteToServerLog($"Автозагрузка ЭСИ: {System.IO.Path.GetFileName(file)}: {ex.Message}");
            System.IO.File.Move(file, System.IO.Path.Combine(errorDir, target));
            System.IO.File.WriteAllText(System.IO.Path.Combine(errorDir, target + ".log"), ex.ToString());
            summary.AppendLine($"{System.IO.Path.GetFileName(file)}: ошибка, {ex.Message}");
        }
    }
    return summary.Length == 0 ? "Новых файлов нет" : summary.ToString();
}

/// <summary>
/// Папка входящих файлов ЭСИ на сервере Союза. Поменяйте путь под себя.
/// </summary>
private const string InboxFolder = @"C:\SoyuzEsiInbox";

/// <summary>
/// Загрузка одного файла. Возвращает текст лога.
/// </summary>
private string LoadFile(string file)
{
    var log = new System.Text.StringBuilder();

    string assemblyDesignation = System.IO.Path.GetFileNameWithoutExtension(file);
    var assembly = FindProducts(assemblyDesignation).FirstOrDefault();
    if (assembly == null)
    {
        throw new HumanFaultException($"Сборка «{assemblyDesignation}» не найдена");
    }

    var composition = assembly.GetAttribute("Items");
    var mesUnit = Service.GetNamedValue(@"STD\MeasureUnits\CompositeLists\MeasureUnitOfQuantity\pcs\pcs");
    var productLink = Service.GetNamedValue(@"STD\ProductLinkType\DesignLink");

    int sortedPos = 1;
    if (composition.CollectionElements.LastOrDefault() != null)
    {
        sortedPos = composition.CollectionElements.LastOrDefault().GetInteger("SortedPosition") + 1;
    }

    using (Service.EnterNewGroupOperation())
    {
        int lineNo = 0;
        foreach (var line in System.IO.File.ReadAllLines(file, System.Text.Encoding.UTF8))
        {
            lineNo++;
            var cells = line.Split(';').Select(a => a.Trim().Trim('"')).ToArray();
            if (cells.Length < 3 || (cells[0] == "" && cells[1] == "") || cells[2] == "")
            {
                continue;
            }

            string designationProduct = cells[0] == "" ? cells[1] : cells[0];
            double resQuantity;
            if (!double.TryParse(cells[2].Replace(',', '.'), System.Globalization.NumberStyles.Any,
                    System.Globalization.CultureInfo.InvariantCulture, out resQuantity))
            {
                // Строка заголовка или мусор
                log.AppendLine($"Строка {lineNo}: пропущена, количество «{cells[2]}» не число");
                continue;
            }

            var found = FindProducts(designationProduct).ToList();
            if (!found.Any())
            {
                log.AppendLine($"Строка {lineNo}: «{designationProduct}» не найдено в базе");
                continue;
            }

            foreach (var item in found)
            {
                if (composition.CollectionElements.Any(a => a.GetInfoObject("ProductConfiguration") == item))
                {
                    log.AppendLine($"Строка {lineNo}: «{item}» уже есть в составе");
                    continue;
                }

                var rowComposition = composition.CreateCollectionElement();
                rowComposition["ProductConfiguration"] = item;
                rowComposition["Quantity"] = resQuantity;
                rowComposition["MeasureUnitOfQuaintity"] = mesUnit;
                rowComposition["LinkType"] = productLink;
                rowComposition["SortedPosition"] = sortedPos;
                composition.AddCollectionElement(rowComposition);
                sortedPos++;

                log.AppendLine($"Строка {lineNo}: добавлено «{item}», {resQuantity}");
            }
        }

        Service.SaveChanges();
    }

    return log.ToString();
}

/// <summary>
/// Поиск исполнений изделия по обозначению или наименованию
/// (тот же поиск, что в «Создать сборку на основании Excel»).
/// </summary>
private IEnumerable<InfoObject> FindProducts(string designation)
{
    var searchOperation = new SearchOperation(EntityIdentifier.InfoObject);
    searchOperation.IsCaseSensitive = false;
    var template1 = Service.GetTemplate(AttributableEntityIdentifier.InfoObject, @"InfoObjects\STD\Products\ProductConfiguration");

    var containerIds = new TID[] { 436, 9, };
    searchOperation.Containers.AddRange(containerIds.Select(id => Service.GetDataContainer(id)).SkipNull());
    searchOperation.IncludeInheritedContainers = true;

    var templateFilter1 = new SearchTemplateFilterItem();
    templateFilter1.FilterByTemplate = template1;
    templateFilter1.IncludeInherited = true;
    {
        var orOp2 = new LogicalOrOperationItem(templateFilter1);
        {
            var attrFilter3 = new SearchAttributeCriterionItem(orOp2);
            attrFilter3.Attribute = template1.GetAttributeDef("Designation").ModifiedClone;
            attrFilter3.OperatorId = RelationalOperator.Equal;
            attrFilter3.Argument1 = designation;
        }
        {
            var attrFilter3 = new SearchAttributeCriterionItem(orOp2);
            attrFilter3.Attribute = template1.GetAttributeDef("Name").ModifiedClone;
            attrFilter3.OperatorId = RelationalOperator.Equal;
            attrFilter3.Argument1 = designation;
        }
    }

    searchOperation.SearchExpressionTree = templateFilter1;
    searchOperation.Execute(false);
    return searchOperation.FoundObjects.OfType<InfoObject>();
}
