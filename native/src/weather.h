#pragma once

#include <QDateTime>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QtQml/qqmlregistration.h>

class QNetworkAccessManager;
class QNetworkReply;

// The weather outside, for the aquarium's surface and light.
//
// With a city set it asks Open-Meteo (free, no key) for the current conditions
// every 30 minutes; the city is geocoded once and its coordinates cached in
// ~/.cache/desktocean/weather.json. Only the city name and the coordinates
// leave the machine. Without a city, or while offline, the weather is made up:
// it changes every 20-60 minutes with odds that follow the season.
//
// Besides the raw conditions it exposes ready-made visual amounts (rain, snow,
// gloom, stir, ...) that ease toward new values over ~20 s, so a change in the
// weather rolls in instead of snapping.
class Weather : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)
    // City name ("Istanbul", "Paris, France") or "lat, lon"; empty = simulated
    Q_PROPERTY(QString city READ city WRITE setCity NOTIFY cityChanged)
    // Development aid: clear|cloudy|fog|drizzle|rain|heavy-rain|snow|storm|wind; empty = real
    Q_PROPERTY(QString forced READ forced WRITE setForced NOTIFY forcedChanged)

    // One of clear, cloudy, fog, drizzle, rain, heavy-rain, snow, storm
    Q_PROPERTY(QString condition READ condition NOTIFY conditionsChanged)
    Q_PROPERTY(qreal intensity READ intensity NOTIFY conditionsChanged)     // 0..1
    Q_PROPERTY(qreal wind READ wind NOTIFY conditionsChanged)               // km/h at 10 m
    Q_PROPERTY(qreal temperature READ temperature NOTIFY conditionsChanged) // °C, NaN if unknown
    Q_PROPERTY(bool isDay READ isDay NOTIFY conditionsChanged)
    // "Open-Meteo", "simulated", "forced" or "off"
    Q_PROPERTY(QString source READ source NOTIFY conditionsChanged)
    // Resolved place name, e.g. "Istanbul, Türkiye"; empty when simulated
    Q_PROPERTY(QString place READ place NOTIFY conditionsChanged)

    // Visual amounts, 0..1, eased
    Q_PROPERTY(qreal rain READ rain NOTIFY visualsChanged)
    Q_PROPERTY(qreal snow READ snow NOTIFY visualsChanged)
    Q_PROPERTY(qreal gloom READ gloom NOTIFY visualsChanged)       // overcast: dimmer, greyer light
    Q_PROPERTY(qreal haze READ haze NOTIFY visualsChanged)         // fog: distance washes out
    Q_PROPERTY(qreal windiness READ windiness NOTIFY visualsChanged)
    // How much the weather stirs the water (added to the CPU agitation)
    Q_PROPERTY(qreal stir READ stir NOTIFY visualsChanged)
    // Seconds between lightning strikes on average; 0 = no lightning
    Q_PROPERTY(qreal lightningEvery READ lightningEvery NOTIFY conditionsChanged)

public:
    explicit Weather(QObject *parent = nullptr);
    ~Weather() override;

    bool enabled() const { return m_enabled; }
    void setEnabled(bool enabled);
    QString city() const { return m_city; }
    void setCity(const QString &city);
    QString forced() const { return m_forced; }
    void setForced(const QString &forced);

    QString condition() const { return m_now.condition; }
    qreal intensity() const { return m_now.intensity; }
    qreal wind() const { return m_now.wind; }
    qreal temperature() const { return m_now.temperature; }
    bool isDay() const { return m_now.isDay; }
    QString source() const { return m_source; }
    QString place() const { return m_place; }

    qreal rain() const { return m_cur.rain; }
    qreal snow() const { return m_cur.snow; }
    qreal gloom() const { return m_cur.gloom; }
    qreal haze() const { return m_cur.haze; }
    qreal windiness() const { return m_cur.wind; }
    qreal stir() const { return m_cur.stir; }
    qreal lightningEvery() const;

    // One line for the diagnostics overlay
    Q_INVOKABLE QString summary() const;

Q_SIGNALS:
    void enabledChanged();
    void cityChanged();
    void forcedChanged();
    void conditionsChanged();
    void visualsChanged();
    // A lightning strike, now; the scene flashes
    void lightning();

private:
    struct Conditions {
        QString condition = QStringLiteral("clear");
        qreal intensity = 0;
        qreal wind = 0;
        qreal temperature = qQNaN();
        bool isDay = true;
    };
    struct Visuals {
        qreal rain = 0, snow = 0, gloom = 0, haze = 0, wind = 0, stir = 0;
    };

    void restart();
    void geocode();
    void fetch();
    void onGeocoded(QNetworkReply *reply);
    void onFetched(QNetworkReply *reply);
    void fallBackToSimulated();
    void simulate();
    void apply(const Conditions &c, const QString &source, bool instant);
    void ease();
    void scheduleLightning();
    void loadCache();
    void saveCache() const;
    static Conditions fromWmo(int code, qreal windKmh, qreal temperature, bool isDay);
    static Visuals visualsFor(const Conditions &c);

    bool m_enabled = true;
    QString m_city;
    QString m_forced;
    // Why the weather is simulated, for the diagnostics ("offline", "city not found")
    QString m_status;

    QNetworkAccessManager *m_net = nullptr;
    QPointer<QNetworkReply> m_pending;
    QTimer m_restartTimer;    // coalesces setting changes
    QTimer m_fetchTimer;      // every 30 min
    QTimer m_simTimer;        // next simulated change
    QTimer m_easeTimer;       // 1 s steps while the visuals ease
    QTimer m_lightningTimer;

    // Geocoded location of m_city
    bool m_haveCoords = false;
    double m_lat = 0, m_lon = 0;
    QString m_place;
    // Last real reading, so a restart or a short outage keeps it
    QDateTime m_fetchedAt;
    Conditions m_fetched;

    Conditions m_now;
    QString m_source = QStringLiteral("simulated");
    Visuals m_cur, m_target;
};
