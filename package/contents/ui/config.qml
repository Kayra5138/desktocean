import QtQuick
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami

Kirigami.FormLayout {
    id: page

    property alias cfg_Fps: fps.value
    property alias cfg_MatchRefresh: matchRefresh.checked
    property alias cfg_PauseOnMaximized: pauseOnMaximized.checked
    property alias cfg_SleepWhenIdle: sleepWhenIdle.checked
    property alias cfg_IdleMinutes: idleMinutes.value
    property string cfg_Theme
    property alias cfg_FollowTimeOfDay: followTimeOfDay.checked
    property alias cfg_Babies: babies.checked
    property alias cfg_MaxFish: maxFish.value
    property alias cfg_ReactToNotifications: reactToNotifications.checked
    property alias cfg_ReactToCpu: reactToCpu.checked
    property alias cfg_ShowNames: showNames.checked
    property alias cfg_Sounds: sounds.checked
    property alias cfg_SoundVolume: soundVolume.value
    property alias cfg_AmbientSound: ambientSound.checked
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

    QQC2.CheckBox {
        id: sleepWhenIdle
        text: "Fish fall asleep when the computer is not used (10 FPS)"
    }

    QQC2.SpinBox {
        id: idleMinutes
        Kirigami.FormData.label: "Fall asleep after (minutes):"
        enabled: sleepWhenIdle.checked
        from: 1
        to: 120
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
        id: showNames
        Kirigami.FormData.label: "Names:"
        text: "Show a fish's name when the cursor is over it"
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
        id: sounds
        Kirigami.FormData.label: "Sounds:"
        text: "Soft sounds: bubbles, feeding, petting, hatching"
    }

    QQC2.Slider {
        id: soundVolume
        Kirigami.FormData.label: "Volume:"
        enabled: sounds.checked
        from: 0
        to: 100
        stepSize: 5
    }

    QQC2.CheckBox {
        id: ambientSound
        enabled: sounds.checked
        text: "Quiet water in the background"
    }

    QQC2.CheckBox {
        id: showDebug
        Kirigami.FormData.label: "Developer:"
        text: "Show diagnostics overlay"
    }
}
