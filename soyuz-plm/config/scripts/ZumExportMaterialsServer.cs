/// <summary>
/// Серверная функция: список материалов с наименованием и обозначением.
/// Отбор тот же, что в queries/materials.sql.
/// </summary>
/// <returns>Массив строк { обозначение, наименование, вид, id }</returns>
public override Object Invoke(UserSession session, Object inputParams)
{
    var ids = new List<TID>();
    using (var r = Service.MT.ExecuteReader(null, MaterialsSql, null))
    {
        while (r.Read())
        {
            ids.Add(r[0].GetValueOfType<TID>());
        }
    }

    var rows = new List<Object[]>();
    foreach (var slice in ids.SliceBy(500))
    {
        var sliceIds = slice.ToArray();
        Service.PreloadInfoObjects(sliceIds, PreloadModeEnum.Attributes);
        foreach (var id in sliceIds)
        {
            var material = Service.GetInfoObject(id);
            if (material == null) continue;
            rows.Add(new Object[] { GetText(material, "Designation"), GetText(material, "Name"), material.Template.NameUI, id.ToString() });
        }
    }
    return rows.ToArray();
}

private static string GetText(InfoObject obj, string nameKey)
{
    try
    {
        return obj.GetString(nameKey) ?? "";
    }
    catch (Exception)
    {
        return "";
    }
}

private const string MaterialsSql = @"
SELECT o0.InfoObjectId, o0.TemplateId, o0.ParentId
FROM InfoObjects AS o0 WITH(NOLOCK)
WHERE o0.Erased = 0
  AND (
        o0.TemplateId IN (1767,   -- Технологический материал (тэшки, услуги)
                          20, 39) -- Материал в сортаменте
     OR (o0.TemplateId = 633      -- Исполнение изделия в версии (ГОСТ 2.053)
         AND EXISTS (
             SELECT 1
             FROM InfoObjectAttributes AS a1 WITH(NOLOCK)
             JOIN InfoObjects AS o1 WITH(NOLOCK) ON o1.InfoObjectId = a1.OwnerId
             WHERE a1.OwnerId = o0.ParentId
               AND o1.Erased = 0
               AND o1.TemplateId IN (432, 25)  -- Версия изделия
               AND a1.DataType = 22            -- класс/доп. шаблон
               AND a1.Outdated = 0
               AND a1.CollectionElementId IS NULL
               AND a1.NameKeyId = 585          -- ProductClass
               AND a1.Link IN (515,  -- Стандартные изделия
                               244,  -- Прочие изделия
                               269)  -- Материалы
         ))
      )
OPTION (MAXDOP 0)";
