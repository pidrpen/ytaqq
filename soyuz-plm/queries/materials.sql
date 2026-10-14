-- Материалы из Союз ПЛМ: технологические материалы, материалы в сортаменте
-- и исполнения изделий, чья версия относится к классам
-- «Стандартные изделия», «Прочие изделия» или «Материалы».
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
OPTION (MAXDOP 0);
