# Сборка FLOWALIGN

C++17, CMake 3.22+, JUCE из ревизии `454fe891acc8204f99f4ef1e3a28c84b1a4da145`. Разместите JUCE рядом с папкой проекта или передайте путь через `-DJUCE_PATH=...`.

```sh
git clone https://github.com/juce-framework/JUCE.git ../JUCE
git -C ../JUCE checkout 454fe891acc8204f99f4ef1e3a28c84b1a4da145
```

## macOS

Нужны Xcode и Python 3. Установщик Universal AU:

```sh
python3 packaging/build_distribution.py --no-dmg
```

## Windows x64

Нужны Visual Studio 2022 с Desktop development with C++, Windows SDK и Inno Setup 6.3+:

```powershell
./packaging/build_windows.ps1
```

Готовые файлы находятся в `dist`. В GitHub Actions доступны ручные сборки macOS installer и Windows installer.

## Переводы

Измените `Source/Translations.json`, затем обновите C++-каталог:

```sh
python3 tools/generate_translations.py
```
