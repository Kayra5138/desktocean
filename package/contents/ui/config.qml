import QtQuick
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami

Kirigami.FormLayout {
    id: page

    property alias cfg_Fps: fps.value
    property alias cfg_MatchRefresh: matchRefresh.checked
    property alias cfg_PauseOnMaximized: pauseOnMaximized.checked
    property string cfg_Theme
    property alias cfg_FollowTimeOfDay: followTimeOfDay.checked
    property alias cfg_Babies: babies.checked
    property alias cfg_MaxFish: maxFish.value
    property alias cfg_ReactToNotifications: reactToNotifications.checked
    property alias cfg_ReactToCpu: reactToCpu.checked
    property alias cfg_ShowDebug: showDebug.checked

    QQC2.SpinBox {
        id: fps
        Kirigami.FormData.label: "Frame rate (FPS):"
        from: 5
        to: 60
        enabled: !matchRefresh.checked
    }

    QQC2.CheckBox {
        id: matchRefresh
        text: "Match the screen's refresh rate (smoothest, uses more CPU)"
    }

    QQC2.CheckBox {
        id: pauseOnMaximized
        Kirigami.FormData.label: "Performance:"
        text: "Pause while a maximized or fullscreen window is open"
    }

    QQC2.ComboBox {
        Kirigami.FormData.label: "Theme:"
        textRole: "text"
        valueRole: "value"
        model: [
            { text: "Tropical", value: "tropical" },
            { text: "Lagoon", value: "lagoon" },
            { text: "Deep sea", value: "deepsea" },
            { text: "Kelp forest", value: "kelp" },
            { text: "Volcanic", value: "volcanic" }
        ]
        Component.onCompleted: currentIndex = Math.max(0, indexOfValue(page.cfg_Theme))
        onActivated: page.cfg_Theme = currentValue
    }

    QQC2.CheckBox {
        id: followTimeOfDay
        Kirigami.FormData.label: "Day and night:"
        text: "Follow the clock (fish rest at night)"
    }

    QQC2.CheckBox {
        id: babies
        Kirigami.FormData.label: "Babies:"
        text: "Close friends have babies now and then"
    }

    QQC2.SpinBox {
        id: maxFish
        Kirigami.FormData.label: "No more babies above:"
        enabled: babies.checked
        from: 2
        to: 100
    }

    QQC2.CheckBox {
        id: reactToNotifications
        Kirigami.FormData.label: "Your computer:"
        text: "A fish blows bubbles when a notification arrives"
    }

    QQC2.CheckBox {
        id: reactToCpu
        text: "Water gets restless when the computer is busy"
    }

    QQC2.CheckBox {
        id: showDebug
        Kirigami.FormData.label: "Developer:"
        text: "Show diagnostics overlay"
    }
}
