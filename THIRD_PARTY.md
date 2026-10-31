# Зависимости

## JUCE

FLOWALIGN использует JUCE 9.0.4, проверенная ревизия `454fe891acc8204f99f4ef1e3a28c84b1a4da145`.

Репозиторий: https://github.com/juce-framework/JUCE. Правообладатель: Raw Material Software Limited. В заголовках используемой ревизии указаны JUCE End User Licence Agreement и альтернативные условия AGPLv3. Условия зависимости и её библиотек находятся в исходниках JUCE; этот репозиторий их не заменяет.

JUCE не включён в дерево исходников. Он подключается отдельно через `JUCE_PATH` или соседнюю папку `../JUCE`.

## Системные средства и иллюстрации

Используются AppKit и AVFoundation из macOS SDK. Установщик создаётся штатными pkgbuild/productbuild, подпись AU — codesign. Эти средства не поставляются внутри репозитория.

Скриншот создан настоящим редактором FLOWALIGN на программно генерируемом аудио. Пользовательские записи и проекты не распространяются.

## Лицензия авторского кода

Авторский код FLOWMINNERS распространяется по MIT (см. LICENSE). Эта лицензия не изменяет условия JUCE, Signalsmith, VST3 SDK и других зависимостей. JUCE используется на условиях JUCE 9 EULA либо AGPLv3; MIT не предоставляет отдельного разрешения на JUCE. Условия: https://juce.com/legal/juce-9-licence/.

## VST3 SDK

Steinberg Media Technologies GmbH, MIT; из закреплённой ревизии JUCE. Текст лицензии: third_party/vst3/LICENSE.txt.
