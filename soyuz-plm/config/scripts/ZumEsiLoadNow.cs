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
/// Команда «Загрузить ЭСИ сейчас»: не дожидаясь расписания, разбирает папку входящих.
/// </summary>
public override Object Invoke(UserSession session, Object inputParams)
{
    Object result;
    using (new WaitCursor())
    {
        result = Service.GetScriptingTask("ZumEsiAutoLoadServer").Invoke(null);
    }
    Service.UI.ShowMessage(result == null ? "Готово" : result.ToString());
    return null;
}
