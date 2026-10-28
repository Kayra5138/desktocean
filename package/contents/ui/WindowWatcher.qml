import QtQuick
import org.kde.taskmanager as TaskManager

// Reports whether a maximized or fullscreen window covers this screen,
// so the aquarium can stop ticking (and stop redrawing) while hidden.
Item {
    id: watcher

    property var screenGeometry
    readonly property bool coveredExists: _covered

    property bool _covered: false

    TaskManager.VirtualDesktopInfo { id: desktopInfo }
    TaskManager.ActivityInfo { id: activityInfo }

    TaskManager.TasksModel {
        id: tasks
        groupMode: TaskManager.TasksModel.GroupDisabled
        screenGeometry: watcher.screenGeometry
        virtualDesktop: desktopInfo.currentDesktop
        activity: activityInfo.currentActivity
        filterByScreen: true
        filterByVirtualDesktop: true
        filterByActivity: true
        filterMinimized: true

        onDataChanged: Qt.callLater(watcher.refresh)
        onCountChanged: Qt.callLater(watcher.refresh)
    }

    function refresh() {
        const roles = TaskManager.AbstractTasksModel;
        let covered = false;
        for (let i = 0; i < tasks.count && !covered; i++) {
            const idx = tasks.index(i, 0);
            if (!tasks.data(idx, roles.IsWindow) || tasks.data(idx, roles.IsMinimized))
                continue;
            covered = tasks.data(idx, roles.IsMaximized) || tasks.data(idx, roles.IsFullScreen);
        }
        _covered = covered;
    }

    Component.onCompleted: refresh()
}
