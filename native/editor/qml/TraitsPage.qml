import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import Desktocean.Editor

// Character and speed of every fish: a card per species with its speed, and a
// row per fish to give it a character or a speed of its own. Changes reach the
// tank at once.
Kirigami.ScrollablePage {
    id: page

    title: "Fish traits"

    readonly property var characters: [
        { text: "Explorer – roams the whole tank", value: 0 },
        { text: "Homebody – keeps to its patch", value: 1 },
        { text: "Bottom dweller – stays low", value: 2 },
        { text: "Skimmer – stays high", value: 3 },
        { text: "Lazy – rests a lot", value: 4 },
        { text: "Playful – darts off in bursts", value: 5 },
        { text: "Shy – keeps away from the cursor", value: 6 }
    ]
    readonly property var speeds: [
        { text: "Very slow", value: 0.25 },
        { text: "Slow", value: 0.5 },
        { text: "Normal", value: 1.0 },
        { text: "Fast", value: 1.7 },
        { text: "Very fast", value: 2.5 }
    ]

    function nearestSpeed(list, value) {
        let best = 0;
        for (let i = 0; i < list.length; ++i) {
            if (Math.abs(list[i].value - value) < Math.abs(list[best].value - value))
                best = i;
        }
        return best;
    }

    LibraryModel {
        id: library
    }

    // All the cards at once, not a ListView: their heights differ, and a list
    // that creates them while scrolling keeps re-estimating its length, which
    // makes the scroll position jump
    ColumnLayout {
        id: list
        spacing: Kirigami.Units.largeSpacing

        Repeater {
        id: cards
        model: library

        delegate: Kirigami.AbstractCard {
            id: card

            required property string fishId
            required property string name
            required property url image
            required property string kind
            required property real speed
            required property var traits

            readonly property bool isFish: kind === "fish"

            Layout.fillWidth: true

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Kirigami.Units.largeSpacing

                    Image {
                        Layout.preferredWidth: Kirigami.Units.gridUnit * 4
                        Layout.preferredHeight: Kirigami.Units.gridUnit * 2.5
                        source: card.image
                        fillMode: Image.PreserveAspectFit
                        asynchronous: true
                        mipmap: true
                    }
                    Kirigami.Heading {
                        Layout.fillWidth: true
                        level: 4
                        text: card.name
                        elide: Text.ElideRight
                    }
                    QQC2.Label { text: "Speed of all:" }
                    QQC2.ComboBox {
                        Layout.preferredWidth: Kirigami.Units.gridUnit * 8
                        // The wheel scrolls the page, it must not change a fish
                        wheelEnabled: false
                        textRole: "text"
                        valueRole: "value"
                        model: page.speeds
                        currentIndex: page.nearestSpeed(page.speeds, card.speed)
                        onActivated: library.setSpeed(card.fishId, currentValue)
                    }
                }

                Repeater {
                    model: card.traits

                    RowLayout {
                        id: row
                        required property int index
                        required property var modelData

                        Layout.fillWidth: true
                        Layout.leftMargin: Kirigami.Units.gridUnit * 2

                        QQC2.Label {
                            Layout.fillWidth: true
                            text: row.modelData.name
                            elide: Text.ElideRight
                        }
                        QQC2.ComboBox {
                            Layout.preferredWidth: Kirigami.Units.gridUnit * 15
                            visible: card.isFish
                            wheelEnabled: false
                            textRole: "text"
                            valueRole: "value"
                            // First "its own", naming what that is when known
                            model: [{
                                text: row.modelData.character < 0 && row.modelData.own
                                      ? "Its own (" + row.modelData.own + ")" : "Its own",
                                value: -1
                            }].concat(page.characters)
                            currentIndex: row.modelData.character + 1
                            onActivated: library.setCopyTraits(card.fishId, row.index, currentValue, row.modelData.speed)
                        }
                        QQC2.ComboBox {
                            id: copySpeed
                            Layout.preferredWidth: Kirigami.Units.gridUnit * 8
                            wheelEnabled: false
                            textRole: "text"
                            valueRole: "value"
                            readonly property var choices: [{ text: "Same as all", value: 0 }].concat(page.speeds)
                            model: choices
                            currentIndex: row.modelData.speed > 0 ? page.nearestSpeed(choices, row.modelData.speed) : 0
                            onActivated: library.setCopyTraits(card.fishId, row.index, row.modelData.character, currentValue)
                        }
                    }
                }
            }
        }

        }

        Kirigami.PlaceholderMessage {
            Layout.fillWidth: true
            visible: cards.count === 0
            icon.name: "view-list-icons"
            text: "No fish yet"
        }
    }
}
