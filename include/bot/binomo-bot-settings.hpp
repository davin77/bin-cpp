/*
* binomo-cpp-api - C ++ API client for binomo
*
* Copyright (c) 2019 Elektro Yar. Email: git.electroyar@gmail.com
*
* Permission is hereby granted, free of charge, to any person obtaining a copy
* of this software and associated documentation files (the "Software"), to deal
* in the Software without restriction, including without limitation the rights
* to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
* copies of the Software, and to permit persons to whom the Software is
* furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
* AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
* OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
* SOFTWARE.
*/
#ifndef BINOMO_BOT_SETTINGS_HPP_INCLUDED
#define BINOMO_BOT_SETTINGS_HPP_INCLUDED

#include "../binomo-cpp-api-common.hpp"
#include <nlohmann/json.hpp>

namespace binomo_bot {
    using json = nlohmann::json;

    /** \brief Класс настроек
     */
    class Settings {
    public:
        std::string json_settings_file;
        std::string path;
        std::string sert_file = "curl-ca-bundle.crt";       /**< Файл сертификата */
        std::string cookie_file = "binomo.cookie";          /**< Файл cookie */
        std::string named_pipe = "binomo_api_bot";          /**< Имя именованного канала */
        std::string symbol_hst_suffix;                      /**< Суффикс имени символа автономных графиков */
        std::vector<std::pair<std::string, uint32_t>> symbols;
        uint32_t candles = 1440;                            /**< Количество баров истории */
        uint32_t max_precisions = 6;                        /**< Максимальная точность котировок, выступает в роли ограничителя */
        int64_t timezone = 0;                               /**< Часовой пояс - смещение метки времени котировок на указанное число секунд */
        bool demo = true;                                   /**< Флаг демо аккаунта */
        bool is_error = false;

        Settings() {};

        Settings(const int argc, char **argv) {
            /* обрабатываем аргументы командой строки */
            json j;
            bool is_default = false;
            if(!binomo_api::common::process_arguments(
                    argc,
                    argv,
                    [&](
                        const std::string &key,
                        const std::string &value) {
                /* аргумент json_file указываает на файл с настройками json */
                if(key == "json_settings_file" || key == "jsf" || key == "jf") {
                    json_settings_file = value;
                }
            })) {
                /* параметры не были указаны */
                if(!binomo_api::common::open_json_file("config.json", j)) {
                    is_error = true;
                    return;
                }
                is_default = true;
            }

            if(!is_default && !binomo_api::common::open_json_file(json_settings_file, j)) {
                is_error = true;
                return;
            }

            /* разбираем json сообщение */
            try {
                if(j["demo"] != nullptr) demo = j["demo"];
                if(j["named_pipe"] != nullptr) named_pipe = j["named_pipe"];
                if(j["symbol_hst_suffix"] != nullptr) symbol_hst_suffix = j["symbol_hst_suffix"];
                if(j["candles"] != nullptr) candles = j["candles"];
                if(j["timezone"] != nullptr) timezone = j["timezone"];
                if(j["path"] != nullptr) path = j["path"];
                if(j["symbols"] != nullptr && j["symbols"].is_array()) {
                    const size_t symbols_size = j["symbols"].size();
                    for(size_t i = 0; i < symbols_size; ++i) {
                        const std::string symbol = j["symbols"][i]["symbol"];
                        const uint32_t period = j["symbols"][i]["period"];
                        symbols.push_back(std::make_pair(symbol,period));
                    }
                }
            }
            catch(const json::parse_error& e) {
                std::cerr << "binomo bot: Settings-->Settings() parser error (json::parse_error), what: " << std::string(e.what()) << std::endl;
                is_error = true;
            }
            catch(const json::out_of_range& e) {
                std::cerr << "binomo bot: Settings-->Settings() parser error (json::out_of_range), what: " << std::string(e.what()) << std::endl;
                is_error = true;
            }
            catch(const json::type_error& e) {
                std::cerr << "binomo bot: Settings-->Settings() parser error (json::type_error), what: " << std::string(e.what()) << std::endl;
                is_error = true;
            }
            catch(...) {
                std::cerr << "binomo bot: Settings-->Settings() parser error" << std::endl;
                is_error = true;
            }
            if(path.size() == 0 || symbols.size() == 0) is_error = true;
        }
    };
}

#endif // BINOMO_BOT_SETTINGS_HPP_INCLUDED
