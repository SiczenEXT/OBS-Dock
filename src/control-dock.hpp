#pragma once

#include <QWidget>
#include <QJsonObject>
#include <QVector>

class QAction;
class QListWidget;
class QToolBar;

class ControlDock final : public QWidget {
public:
    explicit ControlDock(QWidget *parent = nullptr);
    ~ControlDock() override;

    void loadControls();
    void saveControls() const;

private:
    void buildUi();
    void rebuildList();
    void addControl();
    void editSelectedControl();
    void deleteSelectedControl();
    void moveSelectedControl(int direction);
    void executeControl(int row);
    int selectedRow() const;

private:
    QListWidget *list_ = nullptr;
    QToolBar *toolbar_ = nullptr;
    QAction *addAction_ = nullptr;
    QAction *deleteAction_ = nullptr;
    QAction *settingsAction_ = nullptr;
    QAction *upAction_ = nullptr;
    QAction *downAction_ = nullptr;

    QVector<QJsonObject> controls_;
};
