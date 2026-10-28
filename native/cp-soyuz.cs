// cp-soyuz.exe — мост CursorPad → клиент «Союза»: выполнить функцию CursorPadCreate по описанию.
//
//   cp-soyuz.exe <описание cursorpad-*.txt>
//
// Находит через клиентскую службу «Союза» (PLMClientHelperService) клиент, вошедший на
// сервер, и вызывает в нём скриптовую функцию CursorPadCreate (пакет «CursorPad — Союз») с
// путём к описанию — как SDK\Samples\InvokeRunningClient. Пароль не нужен: работает сессия
// того, кто вошёл в клиент, с его правами. Отчёт функции — в консоль (UTF-8). Коды выхода:
// 0 — сделано, 1 — функция ответила «ОШИБКА…», 2 — нет аргумента, 3 — клиент не найден.
// Сборка: build-bridge.ps1 рядом (csc .NET Framework, C# 5). Исходник — домашний plm-create.cs.
using System;
using System.Collections;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.Remoting;
using System.Runtime.Remoting.Channels;
using System.Runtime.Remoting.Channels.Ipc;
using System.Runtime.Serialization.Formatters;
using System.Security.Principal;
using System.Text;
using Microsoft.Win32;

static class PlmCreate
{
    const string TaskKey = "CursorPadCreate";
    static string plmFolder = @"C:\Program Files (x86)\Програмсоюз\BIS v3";

    static int Main(string[] args)
    {
        Console.OutputEncoding = Encoding.UTF8;
        if (args.Length != 1)
        {
            Console.WriteLine("Использование: cp-soyuz.exe <описание cursorpad-*.txt>");
            return 2;
        }
        try
        {
            using (var key = Registry.LocalMachine.OpenSubKey(@"SOFTWARE\ProgramSoyuz\PLM\1.0"))
            {
                var path = key != null ? key.GetValue("Path") as string : null;
                if (!string.IsNullOrEmpty(path))
                    plmFolder = path.TrimEnd('\\');
            }
        }
        catch (Exception)
        {
        }
        AppDomain.CurrentDomain.AssemblyResolve += (s, e) =>
        {
            var name = e.Name.Split(',')[0];
            var loaded = AppDomain.CurrentDomain.GetAssemblies().FirstOrDefault(a => a.GetName().Name.Equals(name, StringComparison.OrdinalIgnoreCase));
            if (loaded != null)
                return loaded;
            foreach (var ext in new[] { ".dll", ".exe" })
            {
                var file = Path.Combine(plmFolder, name + ext);
                if (File.Exists(file))
                    return Assembly.LoadFrom(file);
            }
            return null;
        };
        Assembly.LoadFrom(Path.Combine(plmFolder, "PLMClientHelperService.exe"));
        return Invoke(TaskKey, Path.GetFullPath(args[0]));
    }

    [MethodImpl(MethodImplOptions.NoInlining)]
    static int Invoke(string taskKey, string parameter)
    {
        // канал связи с клиентской службой — как в SDK\Samples\InvokeRunningClient
        var channel = ChannelServices.GetChannel("ipc") as IpcChannel;
        if (channel == null)
        {
            var provider = new BinaryServerFormatterSinkProvider { TypeFilterLevel = TypeFilterLevel.Full };
            IDictionary props = new Hashtable();
            props["portName"] = ProgramSoyuz.ClientHelper.PLMClientHelperService.ChannelName + '.' + Process.GetCurrentProcess().Id;
            var everyone = (NTAccount)new SecurityIdentifier(WellKnownSidType.WorldSid, null).Translate(typeof(NTAccount));
            props["authorizedGroup"] = everyone.Value;
            channel = new IpcChannel(props, null, provider);
            ChannelServices.RegisterChannel(channel, false);
        }
        if (!RemotingConfiguration.GetRegisteredActivatedClientTypes().Any(t => t.ObjectType == typeof(ProgramSoyuz.ClientHelper.RunningClientsManager)))
            RemotingConfiguration.RegisterActivatedClientType(typeof(ProgramSoyuz.ClientHelper.RunningClientsManager),
                "ipc://" + ProgramSoyuz.ClientHelper.PLMClientHelperService.ChannelName);

        var manager = new ProgramSoyuz.ClientHelper.RunningClientsManager();
        manager.OnClientStarted(Process.GetCurrentProcess().Id, plmFolder, null);
        int self = Process.GetCurrentProcess().Id;
        foreach (var client in manager.RunningClients)
        {
            try
            {
                if (!client.IsConnectedToServer)
                    continue;
                if ((client.Capabilities & ProgramSoyuz.ClientHelper.ClientHelperCallbackBase.CapabilitiesFlags.InvokeMethod) == 0)
                    continue;
                if (client.Process.Id == self)
                    continue;
                var callback = client.Callback;
                string server;
                int port;
                if (callback == null || !callback.GetServerAddress(out server, out port))
                    continue;
                Console.WriteLine("Клиент PLM: процесс " + client.Process.Id + ", сервер " + server + ":" + port + ", пользователь " + client.UserId);
                var result = callback.InvokeScriptingTask(taskKey, parameter);
                Console.WriteLine(result != null ? result.ToString() : "(функция ничего не вернула)");
                return result != null && result.ToString().StartsWith("ОШИБКА") ? 1 : 0;
            }
            catch (Exception e)
            {
                Console.WriteLine("Клиент не ответил: " + e.Message);
            }
        }
        Console.WriteLine("Не найден запущенный клиент PLM, вошедший на сервер. Запустите клиент и войдите.");
        return 3;
    }
}
