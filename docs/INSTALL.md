# FLOWALIGN для macOS

1. Скачайте `FLOWALIGN-0.5.0-Universal.pkg` из [релиза](https://github.com/Abynarc/FLOWALIGN/releases/tag/v0.5.0).
2. Сохраните проект и закройте секвенсор.
3. Откройте PKG и выполните установку. Потребуется пароль администратора.
4. В Logic Pro: **Audio FX → Audio Units → FLOWMINNERS → FLOWALIGN**.

macOS 12+, Apple Silicon и Intel. Формат AU. Требования конкретной версии секвенсора учитываются отдельно.

Выберите **RU** или **EN** справа от «Обход». Язык сохраняется в проекте.

Плагин: `/Library/Audio/Plug-Ins/Components/FLOWALIGN.component`.
Резервные копии: `/Library/Application Support/FLOWMINNERS/FLOWALIGN/Backups`.

Установщик сохраняет предыдущую системную версию. Прежняя пользовательская копия текущего пользователя переносится в резервные копии, чтобы не перекрывать системную.

PKG без Developer ID и нотариального заверения Apple; AU подписан ad-hoc. Если macOS блокирует установщик, после попытки открытия используйте **Системные настройки → Конфиденциальность и безопасность → Всё равно открыть**.
