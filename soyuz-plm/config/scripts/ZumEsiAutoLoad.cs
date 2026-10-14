/// <summary>
/// Автоматическое действие по расписанию: загрузка ЭСИ из папки входящих.
/// </summary>
public override void Invoke()
{
    var result = Service.GetScriptingTask("ZumEsiAutoLoadServer").Invoke(null);
    Service.WriteToServerLog($"Автозагрузка ЭСИ: {result}");
}
