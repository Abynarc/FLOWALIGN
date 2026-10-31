# Проверки FLOWALIGN

Тесты используют общий код настоящего плагина. Аудиоданные генерируются синтетически; пользовательские записи не требуются.

Windows: `./packaging/build_windows.ps1 -WithChecks`.
macOS: `cmake -S . -B build-ci -DCMAKE_BUILD_TYPE=Release -DFLOWALIGN_BUILD_VERIFICATION=ON`, затем `cmake --build build-ci --target FLOWALIGN_verify --parallel 4`.

Команды запуска и подготовка тестовых данных находятся в [.github/workflows/macos.yml](../.github/workflows/macos.yml) и [.github/workflows/windows.yml](../.github/workflows/windows.yml). Вывод проверок и код возврата сохраняются в CI.

Автоматические проверки покрывают DSP, сохранение состояния и основные сценарии работы. Прослушивание, PDC/bounce и настоящий перенос регионов в Logic/FL Studio проверяются отдельно в соответствующем хосте.
