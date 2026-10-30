#include "control-dock.hpp"

#include <obs.h>
#include <obs-frontend-api.h>
#include <obs-hotkey.h>
#include <obs-interaction.h>
#include <obs-module.h>

#include <QAbstractItemView>
#include <QAction>
#include <QByteArray>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QIcon>
#include <QItemSelectionModel>
#include <QJsonArray>
#include <QJsonDocument>
#include <QKeyCombination>
#include <QKeySequence>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QMouseEvent>
#include <QSaveFile>
#include <QStackedWidget>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>
#include <QDir>

#include <algorithm>
#include <functional>
#include <utility>

namespace {

QString configFilePath()
{
    char *path = obs_module_config_path("controls.json");
    if (!path)
        return {};

    const QString result = QString::fromUtf8(path);
    bfree(path);
    QDir().mkpath(QFileInfo(result).absolutePath());
    return result;
}

QStringList currentSceneNames()
{
    QStringList result;
    struct obs_frontend_source_list scenes = {};
    obs_frontend_get_scenes(&scenes);

    for (size_t i = 0; i < scenes.sources.num; ++i) {
        obs_source_t *source = scenes.sources.array[i];
        if (source)
            result.append(QString::fromUtf8(obs_source_get_name(source)));
    }

    obs_frontend_source_list_free(&scenes);
    return result;
}

struct SceneItemNameData {
    QStringList *names;
};

bool appendSceneItemName(obs_scene_t *, obs_sceneitem_t *item, void *param)
{
    auto *data = static_cast<SceneItemNameData *>(param);
    if (!data || !data->names || !item)
        return true;

    obs_source_t *source = obs_sceneitem_get_source(item);
    if (source)
        data->names->append(QString::fromUtf8(obs_source_get_name(source)));

    if (obs_sceneitem_is_group(item)) {
        obs_scene_t *groupScene = obs_sceneitem_group_get_scene(item);
        if (groupScene)
            obs_scene_enum_items(groupScene, appendSceneItemName, data);
    }
    return true;
}

QStringList sceneItemNames(const QString &sceneName)
{
    QStringList result;
    const QByteArray encodedName = sceneName.toUtf8();
    obs_source_t *sceneSource = obs_get_source_by_name(encodedName.constData());
    if (!sceneSource)
        return result;

    obs_scene_t *scene = obs_scene_from_source(sceneSource);
    if (scene) {
        SceneItemNameData data{&result};
        obs_scene_enum_items(scene, appendSceneItemName, &data);
    }

    obs_source_release(sceneSource);
    result.removeDuplicates();
    return result;
}

bool appendInputSourceName(void *param, obs_source_t *source)
{
    auto *names = static_cast<QStringList *>(param);
    if (names && source)
        names->append(QString::fromUtf8(obs_source_get_name(source)));
    return true;
}

QStringList allSourceNames()
{
    QStringList result;
    for (const QString &sceneName : currentSceneNames())
        result.append(sceneItemNames(sceneName));

    obs_enum_sources(appendInputSourceName, &result);
    result.removeDuplicates();
    result.sort(Qt::CaseInsensitive);
    return result;
}

void appendFilterName(obs_source_t *, obs_source_t *filter, void *param)
{
    auto *names = static_cast<QStringList *>(param);
    if (names && filter)
        names->append(QString::fromUtf8(obs_source_get_name(filter)));
}

QStringList filterNames(const QString &hostName)
{
    QStringList result;
    const QByteArray encodedName = hostName.toUtf8();
    obs_source_t *host = obs_get_source_by_name(encodedName.constData());
    if (!host)
        return result;

    obs_source_enum_filters(host, appendFilterName, &result);
    obs_source_release(host);
    return result;
}

void refillCombo(QComboBox *combo, const QStringList &values, const QString &keep = {})
{
    const QString oldValue = keep.isEmpty() ? combo->currentText() : keep;
    combo->blockSignals(true);
    combo->clear();
    combo->addItems(values);
    const int existingIndex = oldValue.isEmpty() ? -1 : combo->findText(oldValue);
    if (existingIndex >= 0)
        combo->setCurrentIndex(existingIndex);
    combo->blockSignals(false);
}

QString actionLabel(const QString &key)
{
    if (key == QLatin1String("scene_switch")) return QStringLiteral("Scene: Switch to scene");
    if (key == QLatin1String("scene_item_toggle")) return QStringLiteral("Sources: Toggle visibility in scene");
    if (key == QLatin1String("source_enable_toggle")) return QStringLiteral("Sources: Enable / disable source");
    if (key == QLatin1String("source_mute_toggle")) return QStringLiteral("Sources: Mute / unmute source");
    if (key == QLatin1String("scene_filter_toggle")) return QStringLiteral("Scene filters: Enable / disable filter");
    if (key == QLatin1String("source_filter_toggle")) return QStringLiteral("Source filters: Enable / disable filter");
    if (key == QLatin1String("obs_stream_toggle")) return QStringLiteral("OBS: Start / stop streaming");
    if (key == QLatin1String("obs_record_toggle")) return QStringLiteral("OBS: Start / stop recording");
    if (key == QLatin1String("obs_record_pause")) return QStringLiteral("OBS: Pause / resume recording");
    if (key == QLatin1String("obs_replay_toggle")) return QStringLiteral("OBS: Start / stop replay buffer");
    if (key == QLatin1String("obs_replay_save")) return QStringLiteral("OBS: Save replay");
    if (key == QLatin1String("obs_virtualcam_toggle")) return QStringLiteral("OBS: Start / stop virtual camera");
    if (key == QLatin1String("obs_screenshot")) return QStringLiteral("OBS: Take screenshot");
    if (key == QLatin1String("obs_studio_mode_toggle")) return QStringLiteral("OBS: Toggle Studio Mode");
    if (key == QLatin1String("obs_transition")) return QStringLiteral("OBS: Transition preview to program");
    return QStringLiteral("Unknown action");
}

class ControlListWidget final : public QListWidget {
public:
    using ExecuteCallback = std::function<void(int)>;

    explicit ControlListWidget(ExecuteCallback execute, QWidget *parent = nullptr)
        : QListWidget(parent), execute_(std::move(execute))
    {
        setMouseTracking(true);
    }

protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        const QPoint position = event->position().toPoint();

        if (event->button() == Qt::RightButton) {
            if (QListWidgetItem *item = itemAt(position)) {
                setCurrentItem(item, QItemSelectionModel::ClearAndSelect);
                setFocus(Qt::MouseFocusReason);
            }
            event->accept();
            return;
        }

        if (event->button() == Qt::LeftButton) {
            pressedItem_ = itemAt(position);
            event->accept();
            return;
        }

        QListWidget::mousePressEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton) {
            QListWidgetItem *releasedItem = itemAt(event->position().toPoint());
            if (pressedItem_ && releasedItem == pressedItem_ && execute_) {
                const int clickedRow = row(pressedItem_);
                if (clickedRow >= 0)
                    execute_(clickedRow);
            }
            pressedItem_ = nullptr;
            event->accept();
            return;
        }

        if (event->button() == Qt::RightButton) {
            event->accept();
            return;
        }

        QListWidget::mouseReleaseEvent(event);
    }

    void mouseDoubleClickEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton) {
            event->accept();
            return;
        }
        QListWidget::mouseDoubleClickEvent(event);
    }

private:
    ExecuteCallback execute_;
    QListWidgetItem *pressedItem_ = nullptr;
};

class ControlEditor final : public QDialog {
public:
    explicit ControlEditor(QWidget *parent, const QJsonObject &initial = {}, bool isNew = true)
        : QDialog(parent), initial_(initial)
    {
        setWindowTitle(isNew ? QStringLiteral("Add Control") : QStringLiteral("Control Properties"));
        setModal(true);
        setMinimumWidth(360);

        auto *root = new QVBoxLayout(this);
        auto *form = new QFormLayout();
        nameEdit_ = new QLineEdit(this);
        nameEdit_->setPlaceholderText(QStringLiteral("Control name"));
        nameEdit_->setText(initial.value(QStringLiteral("name")).toString());

        modeCombo_ = new QComboBox(this);
        modeCombo_->addItem(QStringLiteral("Automatic"), QStringLiteral("automatic"));
        modeCombo_->addItem(QStringLiteral("Manual"), QStringLiteral("manual"));
        modeCombo_->setCurrentIndex(initial.value(QStringLiteral("mode")).toString() == QLatin1String("manual") ? 1 : 0);

        form->addRow(QStringLiteral("Control name"), nameEdit_);
        form->addRow(QStringLiteral("Control type"), modeCombo_);
        root->addLayout(form);

        pages_ = new QStackedWidget(this);
        autoPanel_ = new QWidget(pages_);
        manualPanel_ = new QWidget(pages_);
        buildAutomaticPanel();
        buildManualPanel();
        pages_->addWidget(autoPanel_);
        pages_->addWidget(manualPanel_);
        root->addWidget(pages_);

        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        connect(buttons, &QDialogButtonBox::accepted, this, [this]() { acceptIfValid(); });
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        root->addWidget(buttons);

        connect(modeCombo_, &QComboBox::currentIndexChanged, this, [this](int index) {
            pages_->setCurrentIndex(index == 1 ? 1 : 0);
        });
        connect(actionCombo_, &QComboBox::currentIndexChanged, this, [this](int) { updateAutomaticFields(); });
        connect(sceneCombo_, &QComboBox::currentTextChanged, this, [this](const QString &) {
            refillCombo(sourceCombo_, sceneItemNames(sceneCombo_->currentText()), initial_.value(QStringLiteral("source")).toString());
            if (actionCombo_->currentData().toString() == QLatin1String("scene_filter_toggle"))
                refillCombo(filterCombo_, filterNames(sceneCombo_->currentText()));
        });
        connect(hostCombo_, &QComboBox::currentTextChanged, this, [this](const QString &) {
            refillCombo(filterCombo_, filterNames(hostCombo_->currentText()));
        });

        populateInitialValues();
        pages_->setCurrentIndex(modeCombo_->currentData().toString() == QLatin1String("manual") ? 1 : 0);
        updateAutomaticFields();
    }

    QJsonObject result() const
    {
        QJsonObject object;
        object.insert(QStringLiteral("name"), nameEdit_->text().trimmed());
        const QString mode = modeCombo_->currentData().toString();
        object.insert(QStringLiteral("mode"), mode);

        if (mode == QLatin1String("manual")) {
            object.insert(QStringLiteral("hotkey"), keyEdit_->keySequence().toString(QKeySequence::PortableText));
        } else {
            object.insert(QStringLiteral("action"), actionCombo_->currentData().toString());
            object.insert(QStringLiteral("scene"), sceneCombo_->currentText());
            object.insert(QStringLiteral("source"), sourceCombo_->currentText());
            object.insert(QStringLiteral("host"), hostCombo_->currentText());
            object.insert(QStringLiteral("filter"), filterCombo_->currentText());
        }
        return object;
    }

private:
    void buildAutomaticPanel()
    {
        autoForm_ = new QFormLayout(autoPanel_);
        actionCombo_ = new QComboBox(autoPanel_);

        const auto addAction = [this](const QString &label, const QString &key) {
            actionCombo_->addItem(label, key);
        };
        addAction(QStringLiteral("Scene: Switch to scene"), QStringLiteral("scene_switch"));
        addAction(QStringLiteral("Sources: Toggle visibility in scene"), QStringLiteral("scene_item_toggle"));
        addAction(QStringLiteral("Sources: Enable / disable source"), QStringLiteral("source_enable_toggle"));
        addAction(QStringLiteral("Sources: Mute / unmute source"), QStringLiteral("source_mute_toggle"));
        addAction(QStringLiteral("Scene filters: Enable / disable filter"), QStringLiteral("scene_filter_toggle"));
        addAction(QStringLiteral("Source filters: Enable / disable filter"), QStringLiteral("source_filter_toggle"));
        addAction(QStringLiteral("OBS: Start / stop streaming"), QStringLiteral("obs_stream_toggle"));
        addAction(QStringLiteral("OBS: Start / stop recording"), QStringLiteral("obs_record_toggle"));
        addAction(QStringLiteral("OBS: Pause / resume recording"), QStringLiteral("obs_record_pause"));
        addAction(QStringLiteral("OBS: Start / stop replay buffer"), QStringLiteral("obs_replay_toggle"));
        addAction(QStringLiteral("OBS: Save replay"), QStringLiteral("obs_replay_save"));
        addAction(QStringLiteral("OBS: Start / stop virtual camera"), QStringLiteral("obs_virtualcam_toggle"));
        addAction(QStringLiteral("OBS: Take screenshot"), QStringLiteral("obs_screenshot"));
        addAction(QStringLiteral("OBS: Toggle Studio Mode"), QStringLiteral("obs_studio_mode_toggle"));
        addAction(QStringLiteral("OBS: Transition preview to program"), QStringLiteral("obs_transition"));

        sceneCombo_ = new QComboBox(autoPanel_);
        sourceCombo_ = new QComboBox(autoPanel_);
        hostCombo_ = new QComboBox(autoPanel_);
        filterCombo_ = new QComboBox(autoPanel_);

        autoForm_->addRow(QStringLiteral("Action"), actionCombo_);
        autoForm_->addRow(QStringLiteral("Scene"), sceneCombo_);
        autoForm_->addRow(QStringLiteral("Source in scene"), sourceCombo_);
        autoForm_->addRow(QStringLiteral("Source"), hostCombo_);
        autoForm_->addRow(QStringLiteral("Filter"), filterCombo_);
    }

    void buildManualPanel()
    {
        auto *form = new QFormLayout(manualPanel_);
        keyEdit_ = new QKeySequenceEdit(manualPanel_);
        form->addRow(QStringLiteral("Hotkey"), keyEdit_);
        auto *help = new QLabel(QStringLiteral("This runs the matching hotkey already registered in OBS."), manualPanel_);
        help->setWordWrap(true);
        form->addRow(QString(), help);
    }

    void populateInitialValues()
    {
        const QString savedAction = initial_.value(QStringLiteral("action")).toString();
        const int actionIndex = actionCombo_->findData(savedAction);
        if (actionIndex >= 0)
            actionCombo_->setCurrentIndex(actionIndex);

        refillCombo(sceneCombo_, currentSceneNames(), initial_.value(QStringLiteral("scene")).toString());
        refillCombo(sourceCombo_, sceneItemNames(sceneCombo_->currentText()), initial_.value(QStringLiteral("source")).toString());
        refillCombo(hostCombo_, allSourceNames(), initial_.value(QStringLiteral("host")).toString());

        const QString filterHost = savedAction == QLatin1String("scene_filter_toggle")
            ? sceneCombo_->currentText() : hostCombo_->currentText();
        refillCombo(filterCombo_, filterNames(filterHost), initial_.value(QStringLiteral("filter")).toString());

        const QString sequence = initial_.value(QStringLiteral("hotkey")).toString();
        if (!sequence.isEmpty())
            keyEdit_->setKeySequence(QKeySequence::fromString(sequence, QKeySequence::PortableText));
    }

    void setFieldVisible(QWidget *field, bool visible)
    {
        if (QWidget *label = autoForm_->labelForField(field))
            label->setVisible(visible);
        field->setVisible(visible);
    }

    void updateAutomaticFields()
    {
        const QString action = actionCombo_->currentData().toString();
        const bool needsScene = action == QLatin1String("scene_switch") ||
                                action == QLatin1String("scene_item_toggle") ||
                                action == QLatin1String("scene_filter_toggle");
        const bool needsSceneSource = action == QLatin1String("scene_item_toggle");
        const bool needsHost = action == QLatin1String("source_enable_toggle") ||
                               action == QLatin1String("source_mute_toggle") ||
                               action == QLatin1String("source_filter_toggle");
        const bool needsFilter = action == QLatin1String("scene_filter_toggle") ||
                                 action == QLatin1String("source_filter_toggle");

        setFieldVisible(sceneCombo_, needsScene);
        setFieldVisible(sourceCombo_, needsSceneSource);
        setFieldVisible(hostCombo_, needsHost);
        setFieldVisible(filterCombo_, needsFilter);

        if (action == QLatin1String("scene_filter_toggle"))
            refillCombo(filterCombo_, filterNames(sceneCombo_->currentText()));
        else if (action == QLatin1String("source_filter_toggle"))
            refillCombo(filterCombo_, filterNames(hostCombo_->currentText()));
    }

    void acceptIfValid()
    {
        if (nameEdit_->text().trimmed().isEmpty()) {
            QMessageBox::information(this, QStringLiteral("Control name required"),
                                     QStringLiteral("Enter a name for this control."));
            nameEdit_->setFocus();
            return;
        }

        if (modeCombo_->currentData().toString() == QLatin1String("manual")) {
            if (keyEdit_->keySequence().isEmpty()) {
                QMessageBox::information(this, QStringLiteral("Hotkey required"),
                                         QStringLiteral("Press a hotkey combination in the Hotkey field."));
                keyEdit_->setFocus();
                return;
            }
            if (keyEdit_->keySequence().count() != 1) {
                QMessageBox::information(this, QStringLiteral("Single hotkey required"),
                                         QStringLiteral("Use one key combination, such as Ctrl+Shift+F12. Multi-step sequences are not supported."));
                keyEdit_->setFocus();
                return;
            }
        }

        const QString action = actionCombo_->currentData().toString();
        if (modeCombo_->currentData().toString() == QLatin1String("automatic")) {
            if ((action == QLatin1String("scene_switch") || action == QLatin1String("scene_item_toggle") ||
                 action == QLatin1String("scene_filter_toggle")) && sceneCombo_->currentText().isEmpty()) {
                QMessageBox::information(this, QStringLiteral("Scene required"),
                                         QStringLiteral("Add at least one scene in OBS first."));
                return;
            }
            if (action == QLatin1String("scene_item_toggle") && sourceCombo_->currentText().isEmpty()) {
                QMessageBox::information(this, QStringLiteral("Source required"),
                                         QStringLiteral("The selected scene does not contain a source."));
                return;
            }
            if ((action == QLatin1String("source_enable_toggle") || action == QLatin1String("source_mute_toggle") ||
                 action == QLatin1String("source_filter_toggle")) && hostCombo_->currentText().isEmpty()) {
                QMessageBox::information(this, QStringLiteral("Source required"),
                                         QStringLiteral("Add a source in OBS first."));
                return;
            }
            if ((action == QLatin1String("scene_filter_toggle") || action == QLatin1String("source_filter_toggle")) &&
                filterCombo_->currentText().isEmpty()) {
                QMessageBox::information(this, QStringLiteral("Filter required"),
                                         QStringLiteral("The selected target does not have a filter yet."));
                return;
            }
        }
        accept();
    }

    QJsonObject initial_;
    QLineEdit *nameEdit_ = nullptr;
    QComboBox *modeCombo_ = nullptr;
    QStackedWidget *pages_ = nullptr;
    QWidget *autoPanel_ = nullptr;
    QWidget *manualPanel_ = nullptr;
    QFormLayout *autoForm_ = nullptr;
    QComboBox *actionCombo_ = nullptr;
    QComboBox *sceneCombo_ = nullptr;
    QComboBox *sourceCombo_ = nullptr;
    QComboBox *hostCombo_ = nullptr;
    QComboBox *filterCombo_ = nullptr;
    QKeySequenceEdit *keyEdit_ = nullptr;
};

obs_key_t obsKeyForQtKey(int key)
{
    if (key >= Qt::Key_F1 && key <= Qt::Key_F24) {
        const QByteArray name = QByteArrayLiteral("OBS_KEY_F") +
                                QByteArray::number(1 + (key - Qt::Key_F1));
        return obs_key_from_name(name.constData());
    }
    if (key >= Qt::Key_A && key <= Qt::Key_Z) {
        const char letter = static_cast<char>('A' + (key - Qt::Key_A));
        const QByteArray name = QByteArrayLiteral("OBS_KEY_") + QByteArray(1, letter);
        return obs_key_from_name(name.constData());
    }
    if (key >= Qt::Key_0 && key <= Qt::Key_9) {
        const char digit = static_cast<char>('0' + (key - Qt::Key_0));
        const QByteArray name = QByteArrayLiteral("OBS_KEY_") + QByteArray(1, digit);
        return obs_key_from_name(name.constData());
    }

    switch (key) {
    case Qt::Key_Space: return OBS_KEY_SPACE;
    case Qt::Key_Return: return OBS_KEY_RETURN;
    case Qt::Key_Enter: return OBS_KEY_ENTER;
    case Qt::Key_Escape: return OBS_KEY_ESCAPE;
    case Qt::Key_Tab: return OBS_KEY_TAB;
    case Qt::Key_Backspace: return OBS_KEY_BACKSPACE;
    case Qt::Key_Delete: return OBS_KEY_DELETE;
    case Qt::Key_Insert: return OBS_KEY_INSERT;
    case Qt::Key_Left: return OBS_KEY_LEFT;
    case Qt::Key_Right: return OBS_KEY_RIGHT;
    case Qt::Key_Up: return OBS_KEY_UP;
    case Qt::Key_Down: return OBS_KEY_DOWN;
    case Qt::Key_Home: return OBS_KEY_HOME;
    case Qt::Key_End: return OBS_KEY_END;
    case Qt::Key_PageUp: return OBS_KEY_PAGEUP;
    case Qt::Key_PageDown: return OBS_KEY_PAGEDOWN;
    default: return obs_key_from_virtual_key(key);
    }
}

void runManualHotkey(const QString &sequenceText, QObject *timerContext)
{
    const QKeySequence sequence = QKeySequence::fromString(sequenceText, QKeySequence::PortableText);
    if (sequence.isEmpty())
        return;

    const QKeyCombination keyCombination = sequence[0];
    obs_key_combination_t combination{};
    const Qt::KeyboardModifiers modifiers = keyCombination.keyboardModifiers();
    if (modifiers.testFlag(Qt::ShiftModifier)) combination.modifiers |= INTERACT_SHIFT_KEY;
    if (modifiers.testFlag(Qt::ControlModifier)) combination.modifiers |= INTERACT_CONTROL_KEY;
    if (modifiers.testFlag(Qt::AltModifier)) combination.modifiers |= INTERACT_ALT_KEY;
    if (modifiers.testFlag(Qt::MetaModifier)) combination.modifiers |= INTERACT_COMMAND_KEY;
    combination.key = obsKeyForQtKey(keyCombination.key());

    if (combination.key == OBS_KEY_NONE)
        return;

    obs_hotkey_inject_event(combination, true);
    QTimer::singleShot(45, timerContext, [combination]() {
        obs_hotkey_inject_event(combination, false);
    });
}

void executeAutomatic(const QJsonObject &control)
{
    const QString action = control.value(QStringLiteral("action")).toString();
    const QString sceneName = control.value(QStringLiteral("scene")).toString();
    const QString sourceName = control.value(QStringLiteral("source")).toString();
    const QString hostName = control.value(QStringLiteral("host")).toString();
    const QString filterName = control.value(QStringLiteral("filter")).toString();

    if (action == QLatin1String("scene_switch")) {
        const QByteArray name = sceneName.toUtf8();
        obs_source_t *scene = obs_get_source_by_name(name.constData());
        if (scene && obs_source_is_scene(scene))
            obs_frontend_set_current_scene(scene);
        if (scene)
            obs_source_release(scene);
        return;
    }

    if (action == QLatin1String("scene_item_toggle")) {
        const QByteArray sceneEncoded = sceneName.toUtf8();
        const QByteArray sourceEncoded = sourceName.toUtf8();
        obs_source_t *sceneSource = obs_get_source_by_name(sceneEncoded.constData());
        if (!sceneSource)
            return;
        obs_scene_t *scene = obs_scene_from_source(sceneSource);
        obs_sceneitem_t *item = scene ? obs_scene_find_source_recursive(scene, sourceEncoded.constData()) : nullptr;
        if (item)
            obs_sceneitem_set_visible(item, !obs_sceneitem_visible(item));
        obs_source_release(sceneSource);
        return;
    }

    if (action == QLatin1String("source_enable_toggle") || action == QLatin1String("source_mute_toggle")) {
        const QByteArray name = hostName.toUtf8();
        obs_source_t *source = obs_get_source_by_name(name.constData());
        if (!source)
            return;
        if (action == QLatin1String("source_enable_toggle"))
            obs_source_set_enabled(source, !obs_source_enabled(source));
        else
            obs_source_set_muted(source, !obs_source_muted(source));
        obs_source_release(source);
        return;
    }

    if (action == QLatin1String("scene_filter_toggle") || action == QLatin1String("source_filter_toggle")) {
        const QString targetName = action == QLatin1String("scene_filter_toggle") ? sceneName : hostName;
        const QByteArray targetEncoded = targetName.toUtf8();
        const QByteArray filterEncoded = filterName.toUtf8();
        obs_source_t *target = obs_get_source_by_name(targetEncoded.constData());
        if (!target)
            return;
        obs_source_t *filter = obs_source_get_filter_by_name(target, filterEncoded.constData());
        if (filter) {
            obs_source_set_enabled(filter, !obs_source_enabled(filter));
            obs_source_release(filter);
        }
        obs_source_release(target);
        return;
    }

    if (action == QLatin1String("obs_stream_toggle")) {
        if (obs_frontend_streaming_active()) obs_frontend_streaming_stop();
        else obs_frontend_streaming_start();
    } else if (action == QLatin1String("obs_record_toggle")) {
        if (obs_frontend_recording_active()) obs_frontend_recording_stop();
        else obs_frontend_recording_start();
    } else if (action == QLatin1String("obs_record_pause")) {
        if (obs_frontend_recording_active())
            obs_frontend_recording_pause(!obs_frontend_recording_paused());
    } else if (action == QLatin1String("obs_replay_toggle")) {
        if (obs_frontend_replay_buffer_active()) obs_frontend_replay_buffer_stop();
        else obs_frontend_replay_buffer_start();
    } else if (action == QLatin1String("obs_replay_save")) {
        if (obs_frontend_replay_buffer_active()) obs_frontend_replay_buffer_save();
    } else if (action == QLatin1String("obs_virtualcam_toggle")) {
        if (obs_frontend_virtualcam_active()) obs_frontend_stop_virtualcam();
        else obs_frontend_start_virtualcam();
    } else if (action == QLatin1String("obs_screenshot")) {
        obs_frontend_take_screenshot();
    } else if (action == QLatin1String("obs_studio_mode_toggle")) {
        obs_frontend_set_preview_program_mode(!obs_frontend_preview_program_mode_active());
    } else if (action == QLatin1String("obs_transition")) {
        if (obs_frontend_preview_program_mode_active())
            obs_frontend_preview_program_trigger_transition();
    }
}

} // namespace

ControlDock::ControlDock(QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("ControlDockContents"));
    buildUi();
    loadControls();
}

ControlDock::~ControlDock()
{
    saveControls();
}

void ControlDock::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    list_ = new ControlListWidget([this](int row) { executeControl(row); }, this);
    list_->setObjectName(QStringLiteral("ControlDockList"));
    list_->setSelectionMode(QAbstractItemView::SingleSelection);
    list_->setAlternatingRowColors(false);
    list_->setFrameShape(QFrame::NoFrame);
    list_->setToolTip(QStringLiteral("Left-click a control to run it. Right-click a control to select it."));
    root->addWidget(list_, 1);

    // Match OBS's native Scene/Source dock toolbar: QToolBar, 16px icons,
    // non-floating, and icons from OBS's own active-theme Qt resources.
    toolbar_ = new QToolBar(this);
    toolbar_->setObjectName(QStringLiteral("controlDockToolbar"));
    toolbar_->setMovable(false);
    toolbar_->setFloatable(false);
    toolbar_->setIconSize(QSize(16, 16));
    toolbar_->setToolButtonStyle(Qt::ToolButtonIconOnly);

    addAction_ = toolbar_->addAction(QIcon(QStringLiteral(":/res/images/plus.svg")),
                                     QStringLiteral("Add control"));
    deleteAction_ = toolbar_->addAction(QIcon(QStringLiteral(":/res/images/trash.svg")),
                                        QStringLiteral("Delete selected control"));
    toolbar_->addSeparator();
    settingsAction_ = toolbar_->addAction(QIcon(QStringLiteral(":/settings/images/settings/general.svg")),
                                          QStringLiteral("Control properties"));
    toolbar_->addSeparator();
    upAction_ = toolbar_->addAction(QIcon(QStringLiteral(":/res/images/up.svg")),
                                    QStringLiteral("Move selected control up"));
    downAction_ = toolbar_->addAction(QIcon(QStringLiteral(":/res/images/down.svg")),
                                      QStringLiteral("Move selected control down"));

    addAction_->setObjectName(QStringLiteral("addControlAction"));
    deleteAction_->setObjectName(QStringLiteral("deleteControlAction"));
    settingsAction_->setObjectName(QStringLiteral("controlPropertiesAction"));
    upAction_->setObjectName(QStringLiteral("moveControlUpAction"));
    downAction_->setObjectName(QStringLiteral("moveControlDownAction"));

    root->addWidget(toolbar_);

    connect(addAction_, &QAction::triggered, this, [this]() { addControl(); });
    connect(deleteAction_, &QAction::triggered, this, [this]() { deleteSelectedControl(); });
    connect(settingsAction_, &QAction::triggered, this, [this]() { editSelectedControl(); });
    connect(upAction_, &QAction::triggered, this, [this]() { moveSelectedControl(-1); });
    connect(downAction_, &QAction::triggered, this, [this]() { moveSelectedControl(1); });

    connect(list_, &QListWidget::currentRowChanged, this, [this](int row) {
        const bool valid = row >= 0 && row < controls_.size();
        deleteAction_->setEnabled(valid);
        settingsAction_->setEnabled(valid);
        upAction_->setEnabled(valid && row > 0);
        downAction_->setEnabled(valid && row + 1 < controls_.size());
    });

    deleteAction_->setEnabled(false);
    settingsAction_->setEnabled(false);
    upAction_->setEnabled(false);
    downAction_->setEnabled(false);
}

void ControlDock::loadControls()
{
    controls_.clear();
    const QString path = configFilePath();
    if (path.isEmpty()) {
        rebuildList();
        return;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        rebuildList();
        return;
    }

    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    file.close();
    if (document.isArray()) {
        for (const QJsonValue &value : document.array()) {
            if (value.isObject())
                controls_.append(value.toObject());
        }
    }
    rebuildList();
}

void ControlDock::saveControls() const
{
    const QString path = configFilePath();
    if (path.isEmpty())
        return;

    QJsonArray array;
    for (const QJsonObject &control : controls_)
        array.append(control);

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        blog(LOG_WARNING, "[Control Dock] Could not open controls.json for writing.");
        return;
    }
    file.write(QJsonDocument(array).toJson(QJsonDocument::Indented));
    if (!file.commit())
        blog(LOG_WARNING, "[Control Dock] Could not save controls.json.");
}

void ControlDock::rebuildList()
{
    const int oldRow = list_->currentRow();
    list_->clear();

    for (const QJsonObject &control : controls_) {
        const QString name = control.value(QStringLiteral("name")).toString(QStringLiteral("Unnamed control"));
        const bool manual = control.value(QStringLiteral("mode")).toString() == QLatin1String("manual");
        const QString detail = manual
            ? control.value(QStringLiteral("hotkey")).toString()
            : actionLabel(control.value(QStringLiteral("action")).toString());

        auto *item = new QListWidgetItem(name, list_);
        item->setToolTip(detail);
        item->setStatusTip(detail);
        item->setSizeHint(QSize(100, 27));
    }

    if (!controls_.isEmpty() && oldRow >= 0) {
        const int lastRow = static_cast<int>(controls_.size()) - 1;
        list_->setCurrentRow(std::clamp(oldRow, 0, lastRow));
    } else {
        deleteButton_->setEnabled(false);
        settingsButton_->setEnabled(false);
        upButton_->setEnabled(false);
        downButton_->setEnabled(false);
    }
}

int ControlDock::selectedRow() const
{
    const int row = list_->currentRow();
    return row >= 0 && row < controls_.size() ? row : -1;
}

void ControlDock::addControl()
{
    ControlEditor editor(this);
    if (editor.exec() != QDialog::Accepted)
        return;

    controls_.append(editor.result());
    saveControls();
    rebuildList();
}

void ControlDock::editSelectedControl()
{
    const int row = selectedRow();
    if (row < 0)
        return;

    ControlEditor editor(this, controls_.at(row), false);
    if (editor.exec() != QDialog::Accepted)
        return;

    controls_[row] = editor.result();
    saveControls();
    rebuildList();
    list_->setCurrentRow(row);
}

void ControlDock::deleteSelectedControl()
{
    const int row = selectedRow();
    if (row < 0)
        return;

    controls_.removeAt(row);
    saveControls();
    rebuildList();
    list_->setCurrentRow(-1);
}

void ControlDock::moveSelectedControl(int direction)
{
    const int row = selectedRow();
    const int destination = row + direction;
    if (row < 0 || destination < 0 || destination >= controls_.size())
        return;

    controls_.swapItemsAt(row, destination);
    saveControls();
    rebuildList();
    list_->setCurrentRow(destination);
}

void ControlDock::executeControl(int row)
{
    if (row < 0 || row >= controls_.size())
        return;

    const QJsonObject control = controls_.at(row);
    if (control.value(QStringLiteral("mode")).toString() == QLatin1String("manual"))
        runManualHotkey(control.value(QStringLiteral("hotkey")).toString(), this);
    else
        executeAutomatic(control);
}


