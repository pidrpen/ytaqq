# Выгрузка из Союз ПЛМ

Скрипт `export.ps1` подключается к базе Союз ПЛМ (SQL Server), выполняет запросы
из папки `queries` и сохраняет каждый результат в CSV. Ничего ставить не нужно,
хватает PowerShell, который есть в Windows.

## Что внутри

- `config.ini` с адресом сервера, базой, способом входа и папкой для выгрузки.
- `queries/materials.sql` с запросом материалов (технологические материалы,
  материалы в сортаменте, исполнения стандартных и прочих изделий и материалов).
- `export.ps1`, который всё это запускает.

## Как подключить

1. Скопируйте папку `soyuz-plm` на компьютер, откуда видна база Союза.
2. В `config.ini` впишите `Server` и `Database`. Если входите в SQL под своей
   учётной записью Windows, оставьте `Auth = Windows`, иначе поставьте `Sql`
   и заполните `User` и `Password`.
3. Проверьте вручную:
   `powershell -ExecutionPolicy Bypass -File C:\путь\soyuz-plm\export.ps1`
   В `OutputDir` появится папка с датой и файлом `materials.csv`.

## Запуск по расписанию

Чтобы выгрузка шла сама каждый день в 06:00:

```
schtasks /Create /TN "Soyuz export" /SC DAILY /ST 06:00 ^
  /TR "powershell -ExecutionPolicy Bypass -File C:\путь\soyuz-plm\export.ps1"
```

## Новый запрос

Положите `.sql` файл в `queries` и допишите его имя в `Queries` в `config.ini`
через запятую.
