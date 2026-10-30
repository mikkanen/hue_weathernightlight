#include <iostream>
#include <fstream>
#include <string>
#include <memory>
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <hueplusplus/Bridge.h>
#include <hueplusplus/Light.h>
#include <hueplusplus/LinHttpHandler.h>

#include <chrono>
#include <format>
#include <print>
#include <cstdio>
#include "CurlHttpHandler.h"

using json = nlohmann::json;
using namespace hueplusplus;
using namespace std::chrono_literals;


// --- JSON-konfiguraation lukeminen ---
struct Config {
    std::string bridge_ip;
    std::string username;
    std::string location;
    int light_id;
    std::string owm_api_key;
};

// --- Rakenne sää- ja aurinkotiedoille ---
struct WeatherAndSunData {
    std::string tomorrow_weather = "Unknown";
    bool is_night = false;
};

Config loadConfig(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open())
        throw std::runtime_error("Failed to open config file");

    json j;
    file >> j;
    return Config{j["bridge_ip"], j["username"], j["location"], j["light_id"], j["owm_api_key"]};
}

// --- CURL HTTP GET ---
size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}

std::string httpGet(const std::string& url) {
    CURL* curl = curl_easy_init();
    std::string readBuffer;
    if(curl) {
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
        CURLcode res = curl_easy_perform(curl);
        if(res != CURLE_OK)
            std::println(stderr, "curl_easy_perform() failed: {}", curl_easy_strerror(res));
            
        curl_easy_cleanup(curl);
    }
    return readBuffer;
}

WeatherAndSunData getWeatherAndSun(const Config& cfg) {
    WeatherAndSunData result;

    // 1. Haetaan nykyiset aurinkotiedot (sunrise / sunset) /weather-rajapinnasta
    std::string current_url = "http://api.openweathermap.org/data/2.5/weather?q=" + cfg.location +
                              "&appid=" + cfg.owm_api_key;
    
    std::string current_resp = httpGet(current_url);
    if (!current_resp.empty()) {
        try {
            json j_curr = json::parse(current_resp);
            if (j_curr.contains("sys") && j_curr["sys"].contains("sunrise") && j_curr["sys"].contains("sunset")) {
                int64_t sunrise_ts = j_curr["sys"]["sunrise"].get<int64_t>();
                int64_t sunset_ts = j_curr["sys"]["sunset"].get<int64_t>();

                auto now_tp = std::chrono::system_clock::now();
                auto now_ts = std::chrono::duration_cast<std::chrono::seconds>(now_tp.time_since_epoch()).count();

                // On pimeää, jos nykyhetki on ennen auringonnousua tai auringonlaskun jälkeen
                result.is_night = (now_ts < sunrise_ts || now_ts >= sunset_ts);

                // Muunnetaan aikaleimat paikalliseksi kellonajaksi tulostusta varten
                std::time_t sr_time = static_cast<std::time_t>(sunrise_ts);
                std::time_t ss_time = static_cast<std::time_t>(sunset_ts);

                std::tm sr_tm = *std::localtime(&sr_time);
                std::tm ss_tm = *std::localtime(&ss_time);

                std::println("Sijainti: {}", cfg.location);
                std::println("Auringonnousu tänään: {:02d}:{:02d}", sr_tm.tm_hour, sr_tm.tm_min);
                std::println("Auringonlasku tänään: {:02d}:{:02d}", ss_tm.tm_hour, ss_tm.tm_min);
                std::println("Tilanne nyt: {}", result.is_night ? "YÖ (pimeää)" : "PÄIVÄ (valoisaa)");
            }
        } catch (const std::exception& e) {
            std::println(stderr, "JSON parse error (current weather): {}", e.what());
        }
    }

    // Jos on päivä, ei tarvitse hakea ennustetta
    if (!result.is_night) {
        return result;
    }

    // 2. Haetaan huomisen ennuste /forecast-rajapinnasta
    std::string forecast_url = "http://api.openweathermap.org/data/2.5/forecast?q=" + cfg.location +
                               "&appid=" + cfg.owm_api_key;

    std::println("Haetaan huomisen sääennustetta: {}", forecast_url);
    std::string forecast_resp = httpGet(forecast_url);

    if (!forecast_resp.empty()) {
        try {
            json j_fc = json::parse(forecast_resp);

            if (j_fc.contains("list") && j_fc["list"].is_array()) {
                auto now = std::chrono::system_clock::now();
                auto tomorrow = now + 24h;
                std::string tomorrow_str = std::format("{:%Y-%m-%d}", tomorrow);
                std::string target_time = tomorrow_str + " 12:00:00";

                for (const auto& item : j_fc["list"]) {
                    std::string dt_txt = item.value("dt_txt", "");
                    if (dt_txt == target_time || (dt_txt.find(tomorrow_str) != std::string::npos && dt_txt.find("12:00:00") != std::string::npos)) {
                        if (item.contains("weather") && item["weather"].is_array() && !item["weather"].empty()) {
                            result.tomorrow_weather = item["weather"][0]["main"].get<std::string>();
                            break;
                        }
                    }
                }

                if (result.tomorrow_weather == "Unknown") {
                    for (const auto& item : j_fc["list"]) {
                        std::string dt_txt = item.value("dt_txt", "");
                        if (dt_txt.find(tomorrow_str) != std::string::npos) {
                            if (item.contains("weather") && item["weather"].is_array() && !item["weather"].empty()) {
                                result.tomorrow_weather = item["weather"][0]["main"].get<std::string>();
                                break;
                            }
                        }
                    }
                }
            }
        } catch (const std::exception& e) {
            std::println(stderr, "JSON parse error (forecast): {}", e.what());
        }
    }

    return result;
}

// --- Päivitä valo huomisen sään ja yö/päivätilan mukaan ---
void setLightByWeather(Bridge& bridge, int lightId, const WeatherAndSunData& data) {
    Light light = bridge.lights().get(lightId);

    // Jos on päivä, sammutetaan valo
    if (!data.is_night) {
        std::println("Daytime in Mikkeli. Turning off light {}", lightId);
        light.transaction().setOn(false).commit();
        return;
    }

    // Jos on yö, asetetaan valo huomisen sään mukaan
    std::println("Nighttime in Mikkeli. Setting light {} for tomorrow's weather: {}", lightId, data.tomorrow_weather);

    int hue = 12750;
    int sat = 100;
    int bri = 60; // Maltillinen kirkkaus yövaloksi

    if (data.tomorrow_weather == "Rain" || data.tomorrow_weather == "Drizzle") {
        hue = 46920;
        sat = 220;
    } else if (data.tomorrow_weather == "Clouds") {
        hue = 25500;
        sat = 120;
    } else if (data.tomorrow_weather == "Clear") {
        hue = 10000;
        sat = 80;
    } else if (data.tomorrow_weather == "Thunderstorm") {
        hue = 56100;
        sat = 254;
        bri = 150;
    } else if (data.tomorrow_weather == "Snow") {
        hue = 34125;
        sat = 60;
    } else if (data.tomorrow_weather == "Mist" || data.tomorrow_weather == "Fog") {
        hue = 25500;
        sat = 100;
    }

    light.transaction()
         .setBrightness(bri)
         .setColorHue(hue)
         .setColorSaturation(sat)
         .setOn(true)
         .commit();
}

void printAllLights(hueplusplus::Bridge& bridge) {
    std::println("=== Haetaan kaikki valot ===");

    try {
        std::vector<hueplusplus::Light> lights = bridge.lights().getAll();
        std::println("bridge.lights().getAll() onnistui!");
        
        for (const auto& light : lights) {
            std::println("ID: {} - Nimi: {}", light.getId(), light.getName());
        }
    } catch (const std::exception& e) {
        std::println(stderr,"getAll() epäonnistui! Virhe: {}", e.what());
    }

    // Haetaan lista kaikista valoista
    std::vector<hueplusplus::Light> lights = bridge.lights().getAll();

    std::println("bridge.lights().getAll() onnistui!");

    if (lights.empty()) {
        std::println("Ei valoja löytynyt.");
        return;
    }

    for (const auto& light : lights) {
        std::println("----------------------------------------");
        std::println("ID:         {}", light.getId());
        std::println("Nimi:       {}", light.getName());
        std::println("Tyyppi:     {}", light.getType());
        std::println("Tila:       {}", light.isOn() ? "PÄÄLLÄ" : "POIS");
        std::println("Kirkkaus:   {} / 255", static_cast<int>(light.getBrightness()));

        // Värilämpötila (Mired-arvona, jos valo tukee sitä)
        // if (light.hasColorTemperature()) {
        //     std::cout << "Värilämpö:  " << light.getColorTemperature() << " mired" << std::endl;
        // }

        // Värimalli (XY-koordinaatit, jos kyseessä on värivalo)
        //if (light.hasColor()) {
        //    auto [x, y] = light.getColorXY();
        //    std::cout << "Väri (XY):  X=" << x << ", Y=" << y << std::endl;
        //}
    }
    std::println("----------------------------------------");
    std::println("Yhteensä {} valoa löydetty.", lights.size());
}

int main() {
    try {
        Config cfg = loadConfig("config.json");

        // Luo HTTP-handler (esim. LinHttpHandler)
        auto handler = std::make_shared<CurlHttpHandler>();

        // Luo Bridge
        int port = 443; // Oletusportti Hue-sillalle
        Bridge bridge(cfg.bridge_ip, port, cfg.username, handler);

        // Hae aurinko- ja sääennustetiedot
        WeatherAndSunData data = getWeatherAndSun(cfg);

        // printAllLights(bridge);

        // Päivitä valo
        setLightByWeather(bridge, cfg.light_id, data);
        std::println("Light check complete!");

    } catch(const std::exception& e) {
        std::println(stderr, "Error: {}", e.what());
        return 1;
    }
    return 0;
}
