#include "weather.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QUrlQuery>

#include <algorithm>
#include <cmath>

namespace {

constexpr int FetchEveryMs = 30 * 60 * 1000;
constexpr int RetryMs = 10 * 60 * 1000;
constexpr int TimeoutMs = 15 * 1000;
// A reading older than this is not trusted after a restart or outage
constexpr qint64 StaleSecs = 2 * 60 * 60;
// Visuals move this far per second toward a new weather (~20 s for a full change)
constexpr qreal EaseStep = 0.05;

QString cachePath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation)
         + QStringLiteral("/desktocean/weather.json");
}

qreal clamp01(qreal v) { return std::clamp(v, 0.0, 1.0); }

qreal randomRange(qreal lo, qreal hi)
{
    return lo + (hi - lo) * QRandomGenerator::global()->generateDouble();
}

} // namespace

Weather::Weather(QObject *parent)
    : QObject(parent)
{
    m_restartTimer.setSingleShot(true);
    m_restartTimer.setInterval(0);
    connect(&m_restartTimer, &QTimer::timeout, this, &Weather::restart);

    m_fetchTimer.setInterval(FetchEveryMs);
    connect(&m_fetchTimer, &QTimer::timeout, this, &Weather::fetch);

    m_simTimer.setSingleShot(true);
    connect(&m_simTimer, &QTimer::timeout, this, &Weather::simulate);

    m_easeTimer.setInterval(1000);
    connect(&m_easeTimer, &QTimer::timeout, this, &Weather::ease);

    m_lightningTimer.setSingleShot(true);
    connect(&m_lightningTimer, &QTimer::timeout, this, [this] {
        Q_EMIT lightning();
        scheduleLightning();
    });

    // Properties are set right after construction; start once they are in
    m_restartTimer.start();
}

Weather::~Weather()
{
    if (m_pending) {
        m_pending->disconnect(this);
        m_pending->abort();
    }
}

void Weather::setEnabled(bool enabled)
{
    if (enabled == m_enabled)
        return;
    m_enabled = enabled;
    m_restartTimer.start();
    Q_EMIT enabledChanged();
}

void Weather::setCity(const QString &city)
{
    const QString c = city.simplified();
    if (c == m_city)
        return;
    m_city = c;
    // A new place needs new coordinates and a new reading
    m_haveCoords = false;
    m_place.clear();
    m_fetchedAt = {};
    m_restartTimer.start();
    Q_EMIT cityChanged();
}

void Weather::setForced(const QString &forced)
{
    const QString f = forced.trimmed().toLower();
    if (f == m_forced)
        return;
    m_forced = f;
    m_restartTimer.start();
    Q_EMIT forcedChanged();
}

qreal Weather::lightningEvery() const
{
    if (m_source == QLatin1String("off"))
        return 0;
    if (m_now.condition == QLatin1String("storm"))
        return 14 * (1.6 - m_now.intensity);
    if (m_now.condition == QLatin1String("heavy-rain"))
        return 60;
    return 0;
}

QString Weather::summary() const
{
    if (m_source == QLatin1String("off"))
        return QStringLiteral("off");
    QString s = m_now.condition;
    if (m_now.condition != QLatin1String("clear") || m_now.intensity > 0)
        s += QStringLiteral(" %1%").arg(qRound(m_now.intensity * 100));
    s += QStringLiteral(", wind %1 km/h").arg(qRound(m_now.wind));
    if (!std::isnan(m_now.temperature))
        s += QStringLiteral(", %1 °C").arg(qRound(m_now.temperature));
    s += QStringLiteral(" (") + m_source;
    if (!m_place.isEmpty() && m_source == QLatin1String("Open-Meteo"))
        s += QStringLiteral(", ") + m_place;
    if (!m_status.isEmpty())
        s += QStringLiteral(", ") + m_status;
    return s + QLatin1Char(')');
}

void Weather::restart()
{
    m_fetchTimer.stop();
    m_simTimer.stop();
    if (m_pending) {
        m_pending->disconnect(this);
        m_pending->abort();
        m_pending = nullptr;
    }
    m_status.clear();

    if (!m_enabled) {
        apply(Conditions{}, QStringLiteral("off"), false);
        return;
    }
    if (!m_forced.isEmpty()) {
        Conditions c;
        c.intensity = 0.8;
        c.wind = 12;
        if (m_forced == QLatin1String("wind")) {
            c.wind = 55;
            c.intensity = 0;
        } else {
            c.condition = m_forced;
            if (m_forced == QLatin1String("storm"))
                c.wind = 45;
        }
        apply(c, QStringLiteral("forced"), true);
        return;
    }
    if (m_city.isEmpty()) {
        simulate();
        return;
    }
    if (!m_haveCoords)
        loadCache();

    // Real weather; until it arrives, keep the last reading or make some up
    const bool fresh = m_fetchedAt.isValid() && m_fetchedAt.secsTo(QDateTime::currentDateTimeUtc()) < StaleSecs;
    if (fresh)
        apply(m_fetched, QStringLiteral("Open-Meteo"), true);
    else if (m_source != QLatin1String("Open-Meteo"))
        simulate();
    m_fetchTimer.start();
    if (!fresh || m_fetchedAt.secsTo(QDateTime::currentDateTimeUtc()) > FetchEveryMs / 1000)
        fetch();
}

void Weather::fetch()
{
    if (!m_enabled || m_city.isEmpty() || !m_forced.isEmpty())
        return;
    if (!m_haveCoords) {
        geocode();
        return;
    }
    if (m_pending)
        return;
    if (!m_net)
        m_net = new QNetworkAccessManager(this);

    QUrl url(QStringLiteral("https://api.open-meteo.com/v1/forecast"));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("latitude"), QString::number(m_lat, 'f', 3));
    q.addQueryItem(QStringLiteral("longitude"), QString::number(m_lon, 'f', 3));
    q.addQueryItem(QStringLiteral("current"), QStringLiteral("weather_code,wind_speed_10m,temperature_2m,is_day"));
    q.addQueryItem(QStringLiteral("wind_speed_unit"), QStringLiteral("kmh"));
    url.setQuery(q);
    QNetworkRequest req(url);
    req.setTransferTimeout(TimeoutMs);
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Desktocean"));
    m_pending = m_net->get(req);
    QNetworkReply *reply = m_pending;
    connect(reply, &QNetworkReply::finished, this, [this, reply] { onFetched(reply); });
}

void Weather::geocode()
{
    // "41.01, 28.97" needs no lookup
    static const QRegularExpression latLon(QStringLiteral("^(-?\\d+(?:\\.\\d+)?)\\s*[,; ]\\s*(-?\\d+(?:\\.\\d+)?)$"));
    const QRegularExpressionMatch m = latLon.match(m_city);
    if (m.hasMatch()) {
        m_lat = m.captured(1).toDouble();
        m_lon = m.captured(2).toDouble();
        if (std::abs(m_lat) <= 90 && std::abs(m_lon) <= 180) {
            m_haveCoords = true;
            m_place = m_city;
            fetch();
            return;
        }
    }
    if (m_pending)
        return;
    if (!m_net)
        m_net = new QNetworkAccessManager(this);

    // "Paris, France": search for the name, then prefer a result matching the rest
    const QString name = m_city.section(QLatin1Char(','), 0, 0).trimmed();
    QUrl url(QStringLiteral("https://geocoding-api.open-meteo.com/v1/search"));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("name"), name);
    q.addQueryItem(QStringLiteral("count"), QStringLiteral("10"));
    q.addQueryItem(QStringLiteral("language"), QStringLiteral("en"));
    q.addQueryItem(QStringLiteral("format"), QStringLiteral("json"));
    url.setQuery(q);
    QNetworkRequest req(url);
    req.setTransferTimeout(TimeoutMs);
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Desktocean"));
    m_pending = m_net->get(req);
    QNetworkReply *reply = m_pending;
    connect(reply, &QNetworkReply::finished, this, [this, reply] { onGeocoded(reply); });
}

void Weather::onGeocoded(QNetworkReply *reply)
{
    reply->deleteLater();
    m_pending = nullptr;
    if (reply->error() != QNetworkReply::NoError) {
        qInfo("Desktocean: weather: geocoding failed (%s), simulating for now", qPrintable(reply->errorString()));
        m_status = QStringLiteral("offline");
        fallBackToSimulated();
        return;
    }
    const QJsonArray results = QJsonDocument::fromJson(reply->readAll()).object().value(QLatin1String("results")).toArray();
    const QString hint = m_city.section(QLatin1Char(','), 1).trimmed();
    QJsonObject best;
    for (const QJsonValue &v : results) {
        const QJsonObject r = v.toObject();
        if (best.isEmpty())
            best = r;
        if (hint.isEmpty())
            break;
        const QStringList fields{r.value(QLatin1String("country")).toString(), r.value(QLatin1String("country_code")).toString(),
                                 r.value(QLatin1String("admin1")).toString()};
        if (std::any_of(fields.begin(), fields.end(), [&](const QString &f) { return !f.isEmpty() && f.contains(hint, Qt::CaseInsensitive); })) {
            best = r;
            break;
        }
    }
    if (best.isEmpty()) {
        qInfo("Desktocean: weather: no place called \"%s\", simulating", qPrintable(m_city));
        m_status = QStringLiteral("city not found");
        m_fetchTimer.stop();   // asking again won't help
        fallBackToSimulated();
        return;
    }
    m_lat = best.value(QLatin1String("latitude")).toDouble();
    m_lon = best.value(QLatin1String("longitude")).toDouble();
    m_place = best.value(QLatin1String("name")).toString();
    const QString country = best.value(QLatin1String("country")).toString();
    if (!country.isEmpty())
        m_place += QStringLiteral(", ") + country;
    m_haveCoords = true;
    saveCache();
    fetch();
}

void Weather::onFetched(QNetworkReply *reply)
{
    reply->deleteLater();
    m_pending = nullptr;
    const QJsonObject cur = reply->error() == QNetworkReply::NoError
        ? QJsonDocument::fromJson(reply->readAll()).object().value(QLatin1String("current")).toObject()
        : QJsonObject();
    if (cur.isEmpty() || !cur.contains(QLatin1String("weather_code"))) {
        qInfo("Desktocean: weather: fetch failed (%s)", qPrintable(reply->errorString()));
        m_status = QStringLiteral("offline");
        fallBackToSimulated();
        return;
    }
    m_status.clear();
    m_fetched = fromWmo(cur.value(QLatin1String("weather_code")).toInt(),
                        cur.value(QLatin1String("wind_speed_10m")).toDouble(),
                        cur.contains(QLatin1String("temperature_2m")) ? cur.value(QLatin1String("temperature_2m")).toDouble() : qQNaN(),
                        cur.value(QLatin1String("is_day")).toInt(1) != 0);
    m_fetchedAt = QDateTime::currentDateTimeUtc();
    m_simTimer.stop();
    saveCache();
    apply(m_fetched, QStringLiteral("Open-Meteo"), false);
}

void Weather::fallBackToSimulated()
{
    // Keep a recent real reading through a short outage
    if (m_fetchedAt.isValid() && m_fetchedAt.secsTo(QDateTime::currentDateTimeUtc()) < StaleSecs) {
        apply(m_fetched, QStringLiteral("Open-Meteo"), false);
    } else if (m_source != QLatin1String("simulated")) {
        simulate();
    }
    // Try again sooner than the regular half hour
    if (m_fetchTimer.isActive())
        m_fetchTimer.start(RetryMs);
}

void Weather::simulate()
{
    // Season from the month; south of the equator it is the other way round
    int month = QDate::currentDate().month();
    if (m_haveCoords && m_lat < 0)
        month = (month + 5) % 12 + 1;
    //                          clear cloudy fog drizzle rain heavy snow storm
    static const int winter[] = {22, 30, 10, 6, 10, 2, 20, 0};
    static const int shoulder[] = {30, 28, 8, 10, 14, 4, 4, 2};   // March, November
    static const int spring[] = {38, 24, 4, 10, 14, 5, 0, 5};
    static const int summer[] = {55, 15, 2, 5, 10, 5, 0, 8};
    static const int autumn[] = {26, 30, 12, 10, 14, 5, 0, 3};
    const int *odds = (month == 12 || month <= 2) ? winter
                    : (month == 3 || month == 11) ? shoulder
                    : month <= 5 ? spring
                    : month <= 8 ? summer
                                 : autumn;
    static const char *names[] = {"clear", "cloudy", "fog", "drizzle", "rain", "heavy-rain", "snow", "storm"};
    int total = 0;
    for (int i = 0; i < 8; ++i)
        total += odds[i];
    int pick = QRandomGenerator::global()->bounded(total);
    int i = 0;
    while (pick >= odds[i])
        pick -= odds[i++];

    Conditions c;
    c.condition = QString::fromLatin1(names[i]);
    c.intensity = c.condition == QLatin1String("clear") ? 0 : randomRange(0.3, 1.0);
    c.wind = c.condition == QLatin1String("storm")      ? randomRange(30, 65)
           : c.condition == QLatin1String("heavy-rain") ? randomRange(15, 40)
                                                         : randomRange(2, 28);
    const int hour = QTime::currentTime().hour();
    c.isDay = hour >= 6 && hour < 20;
    apply(c, QStringLiteral("simulated"), m_source == QLatin1String("off"));
    m_simTimer.start(int(randomRange(20, 60) * 60 * 1000));
}

Weather::Conditions Weather::fromWmo(int code, qreal windKmh, qreal temperature, bool isDay)
{
    // WMO weather interpretation codes, as used by Open-Meteo
    Conditions c;
    c.wind = windKmh;
    c.temperature = temperature;
    c.isDay = isDay;
    auto set = [&](const char *condition, qreal intensity) {
        c.condition = QString::fromLatin1(condition);
        c.intensity = intensity;
    };
    switch (code) {
    case 0: set("clear", 0); break;
    case 1: set("clear", 0.2); break;            // mainly clear
    case 2: set("cloudy", 0.4); break;           // partly cloudy
    case 3: set("cloudy", 0.8); break;           // overcast
    case 45: set("fog", 0.7); break;
    case 48: set("fog", 0.9); break;             // depositing rime fog
    case 51: set("drizzle", 0.3); break;
    case 53: set("drizzle", 0.6); break;
    case 55: set("drizzle", 0.9); break;
    case 56: set("drizzle", 0.5); break;         // freezing drizzle
    case 57: set("drizzle", 0.9); break;
    case 61: set("rain", 0.4); break;
    case 63: set("rain", 0.7); break;
    case 65: set("heavy-rain", 0.9); break;
    case 66: set("rain", 0.5); break;            // freezing rain
    case 67: set("rain", 0.9); break;
    case 71: set("snow", 0.35); break;
    case 73: set("snow", 0.65); break;
    case 75: set("snow", 1.0); break;
    case 77: set("snow", 0.3); break;            // snow grains
    case 80: set("rain", 0.5); break;            // showers
    case 81: set("rain", 0.8); break;
    case 82: set("heavy-rain", 1.0); break;
    case 85: set("snow", 0.6); break;            // snow showers
    case 86: set("snow", 0.9); break;
    case 95: set("storm", 0.7); break;
    case 96: set("storm", 0.9); break;           // with hail
    case 99: set("storm", 1.0); break;
    default: set(code > 0 ? "cloudy" : "clear", code > 0 ? 0.5 : 0); break;
    }
    return c;
}

Weather::Visuals Weather::visualsFor(const Conditions &c)
{
    Visuals v;
    const qreal i = c.intensity;
    const QString &k = c.condition;
    if (k == QLatin1String("cloudy")) {
        v.gloom = 0.2 + 0.35 * i;
    } else if (k == QLatin1String("fog")) {
        v.gloom = 0.3 + 0.2 * i;
        v.haze = 0.4 + 0.5 * i;
    } else if (k == QLatin1String("drizzle")) {
        v.rain = 0.12 + 0.15 * i;
        v.gloom = 0.4;
    } else if (k == QLatin1String("rain")) {
        v.rain = 0.3 + 0.35 * i;
        v.gloom = 0.5;
    } else if (k == QLatin1String("heavy-rain")) {
        v.rain = 0.7 + 0.3 * i;
        v.gloom = 0.65;
    } else if (k == QLatin1String("snow")) {
        v.snow = 0.25 + 0.75 * i;
        v.gloom = 0.35;
    } else if (k == QLatin1String("storm")) {
        v.rain = 0.8 + 0.2 * i;
        v.gloom = 0.8;
    }
    // Calm below 10 km/h, a gale at 50
    v.wind = clamp01((c.wind - 10) / 40);
    v.stir = clamp01(0.6 * v.wind + (k == QLatin1String("storm") ? 0.3 : 0.1 * v.rain));
    return v;
}

void Weather::apply(const Conditions &c, const QString &source, bool instant)
{
    const bool same = c.condition == m_now.condition && qFuzzyCompare(c.intensity + 1, m_now.intensity + 1)
                   && qFuzzyCompare(c.wind + 1, m_now.wind + 1) && c.isDay == m_now.isDay
                   && source == m_source
                   && (std::isnan(c.temperature) ? std::isnan(m_now.temperature) : c.temperature == m_now.temperature);
    m_now = c;
    m_source = source;
    m_target = source == QLatin1String("off") ? Visuals{} : visualsFor(c);
    if (instant) {
        m_cur = m_target;
        m_easeTimer.stop();
        Q_EMIT visualsChanged();
    } else if (!m_easeTimer.isActive()) {
        m_easeTimer.start();
    }
    scheduleLightning();
    if (!same)
        Q_EMIT conditionsChanged();
}

void Weather::ease()
{
    auto step = [](qreal &cur, qreal target) {
        cur = target > cur ? std::min(target, cur + EaseStep) : std::max(target, cur - EaseStep);
        return cur != target;
    };
    bool moving = false;
    moving |= step(m_cur.rain, m_target.rain);
    moving |= step(m_cur.snow, m_target.snow);
    moving |= step(m_cur.gloom, m_target.gloom);
    moving |= step(m_cur.haze, m_target.haze);
    moving |= step(m_cur.wind, m_target.wind);
    moving |= step(m_cur.stir, m_target.stir);
    if (!moving)
        m_easeTimer.stop();
    Q_EMIT visualsChanged();
}

void Weather::scheduleLightning()
{
    const qreal every = lightningEvery();
    if (every <= 0) {
        m_lightningTimer.stop();
        return;
    }
    if (!m_lightningTimer.isActive())
        m_lightningTimer.start(int(randomRange(0.3, 1.7) * every * 1000));
}

void Weather::loadCache()
{
    QFile f(cachePath());
    if (!f.open(QIODevice::ReadOnly))
        return;
    // Only for the city it was made for
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    if (o.value(QLatin1String("city")).toString() != m_city)
        return;
    m_haveCoords = o.contains(QLatin1String("lat"));
    m_lat = o.value(QLatin1String("lat")).toDouble();
    m_lon = o.value(QLatin1String("lon")).toDouble();
    m_place = o.value(QLatin1String("place")).toString();
    const QJsonObject last = o.value(QLatin1String("last")).toObject();
    if (!last.isEmpty()) {
        m_fetchedAt = QDateTime::fromString(last.value(QLatin1String("at")).toString(), Qt::ISODate);
        m_fetched.condition = last.value(QLatin1String("condition")).toString(QStringLiteral("clear"));
        m_fetched.intensity = last.value(QLatin1String("intensity")).toDouble();
        m_fetched.wind = last.value(QLatin1String("wind")).toDouble();
        m_fetched.temperature = last.contains(QLatin1String("temperature")) ? last.value(QLatin1String("temperature")).toDouble() : qQNaN();
        m_fetched.isDay = last.value(QLatin1String("isDay")).toBool(true);
    }
}

void Weather::saveCache() const
{
    QJsonObject o{{QStringLiteral("city"), m_city}, {QStringLiteral("place"), m_place}};
    if (m_haveCoords) {
        o.insert(QStringLiteral("lat"), m_lat);
        o.insert(QStringLiteral("lon"), m_lon);
    }
    if (m_fetchedAt.isValid()) {
        QJsonObject last{{QStringLiteral("at"), m_fetchedAt.toString(Qt::ISODate)},
                         {QStringLiteral("condition"), m_fetched.condition},
                         {QStringLiteral("intensity"), m_fetched.intensity},
                         {QStringLiteral("wind"), m_fetched.wind},
                         {QStringLiteral("isDay"), m_fetched.isDay}};
        if (!std::isnan(m_fetched.temperature))
            last.insert(QStringLiteral("temperature"), m_fetched.temperature);
        o.insert(QStringLiteral("last"), last);
    }
    const QString path = cachePath();
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(QJsonDocument(o).toJson(QJsonDocument::Compact));
}
