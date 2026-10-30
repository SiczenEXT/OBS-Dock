#pragma once

#include <QWidget>
#include <QJsonObject>
#include <QVector>

class QListWidget;
class QToolButton;

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

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    QListWidget *list_ = nullptr;
    QToolButton *addButton_ = nullptr;
    QToolButton *deleteButton_ = nullptr;
    QToolButton *settingsButton_ = nullptr;
    QToolButton *upButton_ = nullptr;
    QToolButton *downButton_ = nullptr;

    QVector<QJsonObject> controls_;
};
