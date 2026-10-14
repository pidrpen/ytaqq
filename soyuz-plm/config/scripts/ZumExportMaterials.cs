/// <summary>
/// Проверка применимости данного действия в текущем окружении
/// </summary>
public override bool IsValid()
{
    return true;
}

/// <summary>
/// Обновление состояния элементов интерфейса
/// </summary>
public override void UpdateState(ICmdUI cmdUI)
{
    cmdUI.Enabled = true;
}

/// <summary>
/// Команда «Выгрузка материалов»: сохраняет обозначение и наименование всех материалов в CSV.
/// </summary>
public override Object Invoke(UserSession session, Object inputParams)
{
    var saveDialog = new System.Windows.Forms.SaveFileDialog();
    saveDialog.AddExtension = true;
    saveDialog.Filter = "CSV для Excel (*.csv)|*.csv";
    saveDialog.DefaultExt = "csv";
    saveDialog.FileName = "Материалы " + DateTime.Now.ToString("yyyy-MM-dd");
    if (saveDialog.ShowDialog() != System.Windows.Forms.DialogResult.OK)
    {
        return null;
    }

    Object[][] rows;
    using (new WaitCursor())
    {
        rows = Service.GetScriptingTask("ZumExportMaterialsServer").Invoke(null) as Object[][] ?? new Object[0][];
    }

    var text = new System.Text.StringBuilder();
    text.AppendLine("Обозначение;Наименование;Вид;Id");
    foreach (var row in rows)
    {
        text.AppendLine(string.Join(";", row.Select(a => Csv(a == null ? "" : a.ToString()))));
    }
    System.IO.File.WriteAllText(saveDialog.FileName, text.ToString(), new System.Text.UTF8Encoding(true));

    if (Service.UI.ShowMessage($"Выгружено материалов: {rows.Length}. Открыть файл?", System.Windows.Forms.MessageBoxButtons.YesNo, System.Windows.Forms.MessageBoxIcon.Question) == System.Windows.Forms.DialogResult.Yes)
    {
        System.Diagnostics.Process.Start("explorer.exe", saveDialog.FileName);
    }
    return null;
}

private static string Csv(string value)
{
    if (value.IndexOfAny(new[] { ';', '"', '\n', '\r' }) < 0) return value;
    return "\"" + value.Replace("\"", "\"\"") + "\"";
}
