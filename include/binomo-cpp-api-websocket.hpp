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
#ifndef BINOMO_CPP_API_WEBSOCKET_HPP_INCLUDED
#define BINOMO_CPP_API_WEBSOCKET_HPP_INCLUDED

#include "binomo-cpp-api-common.hpp"
#include "client_wss.hpp"
#include <openssl/ssl.h>
#include <wincrypt.h>
#include <xtime.hpp>
#include <nlohmann/json.hpp>
#include <mutex>
#include <atomic>
#include <future>
#include <cstdlib>
//#include "utf8.h" // http://utfcpp.sourceforge.net/

/*
 * клиент иметь несколько конечных точек
 * https://gitlab.com/eidheim/Simple-WebSocket-Server/-/issues/136
 */

namespace binomo_api {
    //using namespace common;

    /** \brief Класс потока котировок для торговли Фьючерсами
     */
    template<class CANDLE = common::Candle>
    class BinomoApiPriceStream {
    private:
        using WssClient = SimpleWeb::SocketClient<SimpleWeb::WSS>;
        using json = nlohmann::json;
        std::string point = "as.binomo.com/";
        std::string sert_file = "curl-ca-bundle.crt";

        std::shared_ptr<WssClient::Connection> save_connection;
        std::shared_ptr<WssClient> client;      /**< Webclosket Клиент */
        std::future<void> client_future;		/**< Поток соединения */
        std::mutex save_connection_mutex;

        std::map<std::string, std::list<uint32_t>> list_subscriptions;
        std::mutex list_subscriptions_mutex;

        //std::map<std::string, common::SymbolConfig> symbols_config;
        //std::mutex symbols_config_mutex;

        using candle_data = std::map<xtime::timestamp_t, CANDLE>;
        using period_data = std::map<uint32_t, candle_data>;
        std::map<std::string, period_data> candles;
        std::mutex candles_mutex;

        std::atomic<bool> is_websocket_init;    /**< Состояние соединения */
        std::atomic<bool> is_error;             /**< Ошибка соединения */
        std::atomic<bool> is_close_connection;  /**< Флаг для закрытия соединения */
        std::atomic<bool> is_open;

        std::atomic<int> volume_mode = ATOMIC_VAR_INIT(0);
        const int MODE_NO_VOLUME = 0;
        const int MODE_VOLUME_ACC = 1;
        const int MODE_VOLUME_WEIGHT_ACC = 2;

        std::string error_message;
        std::recursive_mutex error_message_mutex;
        std::recursive_mutex array_offset_timestamp_mutex;

        const uint32_t array_offset_timestamp_size = 256;
        std::array<xtime::ftimestamp_t, 256> array_offset_timestamp;    /**< Массив смещения метки времени */
        uint8_t index_array_offset_timestamp = 0;                       /**< Индекс элемента массива смещения метки времени */
        uint32_t index_array_offset_timestamp_count = 0;
        xtime::ftimestamp_t last_offset_timestamp_sum = 0;
        std::atomic<double> offset_timestamp = ATOMIC_VAR_INIT(0.0);    /**< Смещение метки времени */
        std::atomic<double> last_timestamp = ATOMIC_VAR_INIT(0.0);

        std::atomic<double> last_server_timestamp = ATOMIC_VAR_INIT(0.0);

        /** \brief Обновить смещение метки времени
         *
         * Данный метод использует оптимизированное скользящее среднее
         * для выборки из 256 элеметов для нахождения смещения метки времени сервера
         * относительно времени компьютера
         * \param offset смещение метки времени
         */
        inline void update_offset_timestamp(const xtime::ftimestamp_t offset) {
            std::lock_guard<std::recursive_mutex> lock(array_offset_timestamp_mutex);

            if(index_array_offset_timestamp_count != array_offset_timestamp_size) {
                array_offset_timestamp[index_array_offset_timestamp] = offset;
                index_array_offset_timestamp_count = (uint32_t)index_array_offset_timestamp + 1;
                last_offset_timestamp_sum += offset;
                offset_timestamp = last_offset_timestamp_sum / (xtime::ftimestamp_t)index_array_offset_timestamp_count;
                ++index_array_offset_timestamp;
                return;
            }
            /* находим скользящее среднее смещения метки времени сервера относительно компьютера */
            last_offset_timestamp_sum = last_offset_timestamp_sum +
                (offset - array_offset_timestamp[index_array_offset_timestamp]);
            array_offset_timestamp[index_array_offset_timestamp++] = offset;
            offset_timestamp = last_offset_timestamp_sum/
                (xtime::ftimestamp_t)array_offset_timestamp_size;
        }

        void send(const std::string &message) {
            std::lock_guard<std::mutex> lock(save_connection_mutex);
            if(!save_connection) return;
            save_connection->send(message);
        }

        /** \brief Парсер сообщения от вебсокета
         * \param response Ответ от сервера
         */
        void parser(const std::string &response) {
            //std::cout << response << std::endl;
            /* Пример сообщений
             * {"data":[{"field":"BTC/USD","action":"subscribe"}],"success":true,"errors":[]}
             * {"data":[{"assets":[{"rate":10800.91635,"precision":5,"repeat":0,"ask":10900.9164,"created_at":"2020-09-27T01:25:08.000000Z","bid":10700.9163,"ric":"BTC/USD"}],"action":"assets"}],"success":true,"errors":[]}
             * {"data":[{"assets":[{"rate":10800.90365,"precision":5,"repeat":0,"ask":10900.9037,"created_at":"2020-09-27T01:25:10.000000Z","bid":10700.9036,"ric":"BTC/USD"}],"action":"assets"}],"success":true,"errors":[]}
             */
            try {
                json j = json::parse(response);
                if(j["success"] == true) {
                    json j_data = j["data"];
                    for(size_t j = 0; j < j_data.size(); ++j) {
                        if(j_data[j]["action"] != "assets") continue;
                        json j_assets = j_data[j]["assets"];
                        for(size_t i = 0; i < j_assets.size(); ++i) {
                            json j_element = j_assets[i];
                            common::StreamTick tick;
                            tick.price = j_element["rate"];
                            tick.precision = j_element["precision"];
                            //j_element["ask"];
                            //j_element["bid"];
                            tick.symbol = j_element["ric"];
                            auto it_normalize_name = common::ric_to_normalize_name.find(tick.symbol);
                            if(it_normalize_name == common::ric_to_normalize_name.end()) continue;
                            tick.symbol = it_normalize_name->second;//common::normalize_symbol_name(tick.symbol);

                            std::string str_iso = j_element["created_at"];
                            xtime::DateTime date_time;
                            if(!xtime::convert_iso(str_iso, date_time)) continue;
                            const double ftimestamp = date_time.get_ftimestamp();
                            tick.timestamp = date_time.get_timestamp();

                            /* проверяем, не поменялась ли метка времени */
                            if(last_timestamp < ftimestamp) {

                                /* если метка времени поменялась, найдем время сервера */
                                xtime::ftimestamp_t pc_timestamp = xtime::get_ftimestamp();
                                xtime::ftimestamp_t offset_timestamp = ftimestamp - pc_timestamp;
                                update_offset_timestamp(offset_timestamp);
                                last_timestamp = ftimestamp;

                                /* запоминаем последнюю метку времени сервера */
                                last_server_timestamp = ftimestamp;
                            }

                            /* обрабатываем функцию обратного вызова поступления тика */
                            if(on_tick != nullptr) on_tick(tick);

                            std::list<uint32_t> list_period;
                            {
                                std::lock_guard<std::mutex> lock(list_subscriptions_mutex);
                                auto it_list_symbol = list_subscriptions.find(tick.symbol);
                                if(it_list_symbol == list_subscriptions.end()) {
                                    continue;
                                }
                                list_period = it_list_symbol->second;
                            }

                            std::lock_guard<std::mutex> lock(candles_mutex);
                            for(auto &p : list_period) {

                                /* в ходе наблюдений было обнаружено,
                                 * что 0-секунда считается прыдудщим баром, а не новым
                                 * поэтому нужно вычесть 1 секунду из метки времени
                                 */
                                const xtime::timestamp_t timestamp = tick.timestamp - 1;

                                /* в ходе наблюдений было обнаружено,
                                 * что время бара взято как время окончания бара
                                 * а не время начала
                                 */
                                const xtime::timestamp_t bar_timestamp = (timestamp - (timestamp % (p))) + p;

                                auto it_symbol = candles.find(tick.symbol);
                                if(it_symbol == candles.end()) {
                                    /* если символ не найден, значит бара вообще нет. Инициализируем */
                                    CANDLE candle(tick.price,tick.price,tick.price,tick.price, bar_timestamp);
                                    if(volume_mode == MODE_VOLUME_ACC) {
                                        candle.volume = 1;
                                    } else
                                    if(volume_mode == MODE_VOLUME_WEIGHT_ACC) {
                                        candle.volume = 0;
                                    }
                                    candles[tick.symbol][(p)][bar_timestamp] = candle;
                                    if(on_candle != nullptr) on_candle(tick.symbol, candle, p,  false);
                                } else {
                                    /* символ найдет, ищем период */
                                    auto it_period = it_symbol->second.find(p);
                                    if(it_period == it_symbol->second.end()) {
                                        /* период не найден, значит бара вообще нет. Инициализируем */
                                        CANDLE candle(tick.price,tick.price,tick.price,tick.price,bar_timestamp);
                                        if(volume_mode == MODE_VOLUME_ACC) {
                                            candle.volume = 1;
                                        } else
                                        if(volume_mode == MODE_VOLUME_WEIGHT_ACC) {
                                            candle.volume = 0;
                                        }
                                        candles[tick.symbol][(p)][bar_timestamp] = candle;
                                        if(on_candle != nullptr) on_candle(tick.symbol, candle, p, false);
                                    } else {
                                        /* период найдет, ищем бар */
                                        auto it_last_candle = it_period->second.find(bar_timestamp);
                                        if(it_last_candle == it_period->second.end()) {
                                            /* бар не найден */
                                            if(it_period->second.size() > 0) {
                                                /* если данные уже есть */
                                                auto it_candle = it_period->second.begin();
                                                /* получаем последний бар, вызываем функцию обратного вызова */
                                                std::advance(it_candle, it_period->second.size() - 1);
                                                if(on_candle != nullptr) on_candle(tick.symbol, it_candle->second, p, true);
                                            }
                                            /* добавляем бар */
                                            CANDLE candle(tick.price,tick.price,tick.price,tick.price,bar_timestamp);
                                            if(volume_mode == MODE_VOLUME_ACC) {
                                                candle.volume = 1;
                                            } else
                                            if(volume_mode == MODE_VOLUME_WEIGHT_ACC) {
                                                candle.volume = 0;
                                            }
                                            candles[tick.symbol][p][bar_timestamp] = candle;
                                            if(on_candle != nullptr) on_candle(tick.symbol, candle, p, false);
                                        } else {
                                            auto &candle = it_last_candle->second;
                                            if(volume_mode == MODE_VOLUME_ACC) {
                                                candle.volume += 1.0d;
                                            } else
                                            if(volume_mode == MODE_VOLUME_WEIGHT_ACC) {
                                                if(candle.volume == 0) {
                                                    auto it_candle = it_period->second.begin();
                                                    /* получаем последний бар, вызываем функцию обратного вызова */
                                                    std::advance(it_candle, it_period->second.size() - 1);
                                                    const double diff = std::abs(tick.price - it_candle->second.close);
                                                    candle.volume += (std::pow(10.0d, tick.precision) * diff + 0.5d);
                                                } else {
                                                    const double diff = std::abs(tick.price - candle.close);
                                                    candle.volume += (std::pow(10.0d, tick.precision) * diff + 0.5d);
                                                }
                                            }
                                            candle.close = tick.price;
                                            if(tick.price > candle.high) candle.high = tick.price;
                                            if(tick.price < candle.low) candle.low = tick.price;
                                            if(on_candle != nullptr) on_candle(tick.symbol, candle, p, false);
                                        }
                                    }
                                }
                            } // for
                        } // for i
                    } // for j
                }
                is_websocket_init = true;
            }
            catch(const json::parse_error& e) {
                std::cerr << "binomo api: BinomoApiPriceStream--->parser json::parse_error, what = " << e.what()
                   << " exception_id = " << e.id << std::endl;
            }
            catch(json::out_of_range& e) {
                std::cerr << "binomo api: BinomoApiPriceStream--->parser json::out_of_range, what = " << e.what()
                   << " exception_id = " << e.id << std::endl;
            }
            catch(json::type_error& e) {
                std::cerr << "binomo api: BinomoApiPriceStream--->parser json::type_error, what = " << e.what()
                   << " exception_id = " << e.id << std::endl;
            }
            catch(...) {
                std::cerr << "binomo api: BinomoApiPriceStream--->parser json error" << std::endl;
            }
        }

    public:

        std::function<void(
            const std::string &symbol,
            const CANDLE &candle,
            const uint32_t period,
            const bool close_candle)> on_candle = nullptr;

        std::function<void(const common::StreamTick &tick)> on_tick = nullptr;
        std::function<void()> on_start = nullptr;

        /** \brief Конструктор класса для получения потока котировок
         * \param user_sert_file Файл-сертификат. По умолчанию используется от curl: curl-ca-bundle.crt
         */
        BinomoApiPriceStream(const std::string user_sert_file = "curl-ca-bundle.crt") {
            sert_file = user_sert_file;
            offset_timestamp = 0;
            is_websocket_init = false;
            is_close_connection = false;
            is_error = false;
            is_open = false;
        }

        ~BinomoApiPriceStream() {
            is_close_connection = true;
            std::shared_ptr<WssClient> client_ptr = std::atomic_load(&client);
            if(client_ptr) {
                client_ptr->stop();
            }

            if(client_future.valid()) {
                try {
                    client_future.wait();
                    client_future.get();
                }
                catch(const std::exception &e) {
                    std::cerr << "binomo api: ~BinomoApiPriceStream() error, what: " << e.what() << std::endl;
                }
                catch(...) {
                    std::cerr << "binomo api: ~BinomoApiPriceStream() error" << std::endl;
                }
            }
        };

        /** \brief Состояние соединения
         * \return вернет true, если соединение есть
         */
        inline bool connected() {
            return is_websocket_init;
        }

        /** \brief Подождать соединение
         *
         * Данный метод ждет, пока не установится соединение
         * \return вернет true, если соединение есть, иначе произошла ошибка
         */
        inline bool wait() {
            uint32_t tick = 0;
            while(!is_error && !is_open && !is_close_connection) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                ++tick;
                const uint32_t MAX_TICK = 10*100*5;
                if(tick > MAX_TICK) {
                    is_error = true;
                    return is_open;
                }
            }
            return is_open;
        }

        /** \brief Получить метку времени сервера
         *
         * Данный метод возвращает метку времени сервера. Часовая зона: UTC/GMT
         * \return Метка времени сервера
         */
        inline xtime::ftimestamp_t get_server_timestamp() {
            return xtime::get_ftimestamp() + offset_timestamp;
        }

        /** \brief Получить последнюю метку времени сервера
         *
         * Данный метод возвращает последнюю полученную метку времени сервера. Часовая зона: UTC/GMT
         * \return Метка времени сервера
         */
        inline xtime::ftimestamp_t get_last_server_timestamp() {
            return last_server_timestamp;
        }

        /** \brief Получить смещение метки времени ПК
         * \return Смещение метки времени ПК
         */
        inline xtime::ftimestamp_t get_offset_timestamp() {
            return offset_timestamp;
        }

        /** \brief Получить цену тика символа
         *
         * \param symbol Имя символа
         * \param period Период
         * \return Последняя цена bid
         */
        inline double get_price(const std::string &symbol, const uint32_t period) {
            if(!is_websocket_init) return 0.0;
            std::string s = common::to_upper_case(symbol);
            std::lock_guard<std::recursive_mutex> lock(candles_mutex);
            auto it_symbol = candles.find(s);
            if(it_symbol == candles.end()) return 0.0;
            auto it_period = it_symbol->second.find(period);
            if(it_period == it_symbol->second.end()) return 0.0;
            const size_t size_candle = it_period->second.size();
            if(size_candle == 0) return 0.0;
            auto it_begin_candle = it_period->second.begin();
            std::advance(it_begin_candle, (size_candle - 1));
            return it_begin_candle->second.close;
        }

        /** \brief Получить цену тика символа
         * \param symbol Имя символа
         * \return Последняя цена bid
         */
        inline double get_price(const std::string &symbol) {
            if(!is_websocket_init) return 0.0;
            std::string s = common::to_upper_case(symbol);
            std::lock_guard<std::recursive_mutex> lock(candles_mutex);
            auto it_symbol = candles.find(s);
            if(it_symbol == candles.end()) return 0.0;
            if(it_symbol->second.size() == 0) return 0.0;
            auto it_period = it_symbol->second.begin();
            auto it_begin_candle = it_period->second.begin();
            return it_begin_candle->second.close;
        }

        /** \brief Получить бар
         *
         * \param symbol Имя символа
         * \param period Период
         * \param offset Смещение
         * \return Бар
         */
        inline CANDLE get_candle(
                const std::string &symbol,
                const uint32_t period,
                const size_t offset = 0) {
            if(!is_websocket_init) return CANDLE();
            std::string s = common::to_upper_case(symbol);
            std::lock_guard<std::mutex> lock(candles_mutex);
            auto it_symbol = candles.find(s);
            if(it_symbol == candles.end()) return CANDLE();
            auto it_period = it_symbol->second.find(period);
            if(it_period == it_symbol->second.end()) return CANDLE();
            const size_t size_candle = it_period->second.size();
            if(size_candle == 0) return CANDLE();
            if(offset >= size_candle) return CANDLE();
            auto it_begin_candle = it_period->second.begin();
            std::advance(it_begin_candle, (size_candle - 1 - offset));
            return it_begin_candle->second;
        }

        /** \brief Получить количество баров
         * \param symbol Имя символа
         * \param period Период
         * \return Количество баров
         */
        inline uint32_t get_num_candles(
                const std::string &symbol,
                const uint32_t period) {
            if(!is_websocket_init) return 0;
            std::string s = common::to_upper_case(symbol);
            std::lock_guard<std::recursive_mutex> lock(candles_mutex);
            auto it_symbol = candles.find(s);
            if(it_symbol == candles.end()) return 0;
            auto it_period = it_symbol->second.find(period);
            if(it_period == it_symbol->second.end()) return 0;
            return it_period->second.size();
        }

        /** \brief Получить бар по метке времени
         * \param symbol Имя символа
         * \param period Период
         * \param timestamp Метка времени
         * \return Бар
         */
        inline CANDLE get_timestamp_candle(
                const std::string &symbol,
                const uint32_t period,
                const xtime::timestamp_t timestamp) {
            if(!is_websocket_init) return CANDLE();
            std::string s = common::to_upper_case(symbol);
            std::lock_guard<std::mutex> lock(candles_mutex);
            auto it_symbol = candles.find(s);
            if(it_symbol == candles.end()) return CANDLE();
            auto it_period = it_symbol->second.find(period);
            if(it_period == it_symbol->second.end()) return CANDLE();
            if(it_period->second.size() == 0) return CANDLE();
            auto it_candle = it_period->second.find(timestamp);
            if(it_candle == it_period->second.end()) return CANDLE();
            return it_candle->second;
        }

        /** \brief Инициализировать массив японских свечей
         * \param symbol Имя символа
         * \param period Период
         * \param new_candles Массив баров
         * \return Код ошибки, вернет 0 если все в порядке
         */
        template<class T>
        int init_array_candles(
                const std::string &symbol,
                const uint32_t period,
                const T &new_candles) {
            std::string s = common::to_upper_case(symbol);
            std::lock_guard<std::recursive_mutex> lock(candles_mutex);
            auto it_symbol = candles.find(s);
            if(it_symbol == candles.end()) {
                candles.insert(
                    std::pair<std::string, period_data>(s, period_data()));
                it_symbol = candles.find(s);
            }
            auto it_period = it_symbol->second.find(period);
            if(it_period == it_symbol->second.end()) {
                it_symbol->second.insert(
                    std::pair<uint32_t, candle_data>(period, candle_data()));
                it_period = it_symbol->second.find(period);
            }

            for(auto &candle : new_candles) {
                it_period->second.insert(
                    std::pair<xtime::timestamp_t, CANDLE>(candle.timestamp, candle));
            }
            return common::OK;
        }

        /** \brief Ждать закрытие бара (минутного)
         * \param f Лямбда-функция, которую можно использовать как callbacks
         */
        inline void wait_candle_close(std::function<void(
                const xtime::ftimestamp_t timestamp,
                const xtime::ftimestamp_t timestamp_stop)> f = nullptr) {
            const xtime::ftimestamp_t timestamp_stop =
                xtime::get_first_timestamp_minute(get_server_timestamp()) +
                xtime::SECONDS_IN_MINUTE;
            while(!is_close_connection) {
                const xtime::ftimestamp_t t = get_server_timestamp();
                if(t >= timestamp_stop) break;
                if(f != nullptr) f(t, timestamp_stop);
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }

        /** \brief Проверить наличие ошибки
         * \return вернет true, если была ошибка
         */
        inline bool check_error() {
            return is_error;
        }

        /** \brief Очистить состояние ошибки
         */
        inline void clear_error() {
            is_error = false;
            std::lock_guard<std::recursive_mutex> lock(error_message_mutex);
            error_message.clear();
        }

        /** \brief Получить текст сообщения об ошибке
         * \return сообщения об ошибке, если есть
         */
        std::string get_error_message() {
            std::lock_guard<std::recursive_mutex> lock(error_message_mutex);
            if(is_error) return error_message;
            return std::string();
        }

        /** \brief Подписаться на поток котировок символа
         * \param symbol Имя символа
         */
        void subscribe_symbol(const std::string &symbol) {
            // {"action":"subscribe","rics":["BTC/USD"]}
            std::string s = common::to_upper_case(symbol);
            s = binomo_api::common::normalize_symbol_name(s);
            auto it_ric = binomo_api::common::normalize_name_to_ric.find(s);
            if(it_ric == binomo_api::common::normalize_name_to_ric.end()) {
                std::cerr << "binomo api: symbol " << s << " does not exist!" << std::endl;
                return;
            }
            json j;
            j["action"] = "subscribe";
            j["rics"] = json::array();
            j["rics"][0] = it_ric->second;
            send(j.dump());
        }

        /** \brief Одписаться от потока котировок символа
         * \param symbol Имя символа
         */
        void unsubscribe_symbol(const std::string &symbol) {
            // {"action":"unsubscribe","rics":["BTC/LTC"]}
            std::string s = common::to_upper_case(symbol);
            s = binomo_api::common::normalize_symbol_name(s);
            auto it_ric = binomo_api::common::normalize_name_to_ric.find(s);
            if(it_ric == binomo_api::common::normalize_name_to_ric.end()) {
                std::cerr << "binomo api: symbol " << s << " does not exist!" << std::endl;
                return;
            }
            json j;
            j["action"] = "unsubscribe";
            j["rics"] = json::array();
            j["rics"][0] = it_ric->second;
            send(j.dump());
        }

        /** \brief Подписаться на поток котировок символов
         * \param symbols Имена символов
         */
        void subscribe_symbols(const std::vector<std::string> &symbols) {
            // {"action":"subscribe","rics":["BTC/USD"]}
            json j;
            j["action"] = "subscribe";
            j["rics"] = json::array();
            for(size_t i = 0; i < symbols.size(); ++i) {
                std::string s = common::to_upper_case(symbols[i]);
                s = binomo_api::common::normalize_symbol_name(s);
                auto it_ric = binomo_api::common::normalize_name_to_ric.find(s);
                if(it_ric == binomo_api::common::normalize_name_to_ric.end()) {
					std::cerr << "binomo api: symbol " << s << " does not exist!" << std::endl;
                    continue;
				}
                j["rics"][i] = it_ric->second;
            }
            send(j.dump());
        }

        /** \brief Отписаться от потока котировок символов
         * \param symbols Имена символов
         */
        void unsubscribe_symbols(const std::vector<std::string> &symbols) {
            // {"action":"unsubscribe","rics":["BTC/LTC"]}
            json j;
            j["action"] = "unsubscribe";
            j["rics"] = json::array();
            for(size_t i = 0; i < symbols.size(); ++i) {
                std::string s = common::to_upper_case(symbols[i]);
                s = binomo_api::common::normalize_symbol_name(s);
                auto it_ric = binomo_api::common::normalize_name_to_ric.find(s);
                if(it_ric == binomo_api::common::normalize_name_to_ric.end()) {
					std::cerr << "binomo api: symbol " << s << " does not exist!" << std::endl;
                    continue;
				}
                j["rics"][i] = it_ric->second;
            }
            send(j.dump());
        }

        /** \brief Подписаться на котировки
         * \param symbol_list Список имен символов/валютных пар с периодом
         * \return Вернет true, если подключение есть и сообщения были переданы
         */
        bool add_candles_stream(const std::vector<std::pair<std::string, uint32_t>> &symbol_list) {
            std::lock_guard<std::mutex> lock(list_subscriptions_mutex);
            for(auto &symbol : symbol_list) {
                std::string s = binomo_api::common::normalize_symbol_name(symbol.first);
				auto it_ric = binomo_api::common::normalize_name_to_ric.find(s);
				if(it_ric == binomo_api::common::normalize_name_to_ric.end()) {
					std::cerr << "binomo api: symbol " << s << " does not exist!" << std::endl;
                    continue;
				}
                list_subscriptions[symbol.first].push_back(symbol.second);
            }
            return true;
        }

        void set_volume_mode(const int value) {
            volume_mode = value;
        }
#if(0)
		/** \brief Получить количество знаков после запятой
         * \param symbol Имя символа
         * \return количество знаков после запятой
         */
        inline uint32_t get_precision(const std::string &symbol) {
            std::lock_guard<std::mutex> lock(symbols_config_mutex);
            auto it_config = symbols_config.find(symbol);
            if(it_config == symbols_config.end()) return 0;
            return it_config->second.precision;
        }

        /** \brief Отписаться от котировок
         * \return Вернет true, если подключение есть и сообщения были переданы
         */
        bool unsubscribe_quotes_all_stream() {
            if(!is_connected) return false;
            std::map<uint32_t, std::pair<std::string, uint32_t>> copy_sid_to_symbol_name;
            {
                std::lock_guard<std::mutex> lock(sid_to_symbol_name_mutex);
                copy_sid_to_symbol_name = sid_to_symbol_name;
            }
            for(auto symbol : copy_sid_to_symbol_name) {
                // {"type":"UNSUBSCRIPTION","rid":24,"action":"tickers","body":{"sid":7}}
                const uint64_t current_rid = rid_counter++;
                json j;
                j["type"] = "UNSUBSCRIPTION";
                j["rid"] = current_rid;
                j["action"] = "bar";
                //j["body"]["symbolId"] = symbol_id;
                //j["body"]["barSize"] = bar_size;
                j["body"]["sid"] = symbol.first;
                send(j.dump());
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            return true;
        }


        /** \brief Добавить поток символа с заданным периодом
         * \param symbol Имя символа
         */
        void add_symbol_stream(const std::string &symbol, ) {
            // {"action":"subscribe","rics":["BTC/USD"]}
            std::string s = to_upper_case(symbol);
            if(s == "ZCRYIDX") s.insert(1, 1, '-');
            if(s != "Z-CRYIDX") s.insert(3, 1, '/');
            else s.insert(5, 1, '/');
            json j;
            j["action"] = "subscribe";
            j["rics"] = json::array();
            j["rics"][0] = s;

            std::string param = s + "@kline_" + it->second;
            j["params"][0] = param;
            j["id"] = 1;
            {
                std::lock_guard<std::mutex> lock(list_subscriptions_mutex);
                /* имя символа ОБЯЗАТЕЛЬНО В НИЖНЕМ РЕГИСТРЕ! */
                list_subscriptions[s][period] = true;
            }
            if(!is_open) return;
            send(j.dump());
        }

        /** \brief Убрать поток символа с заданным периодом
         * \param symbol Имя символа
         * \param period Период
         */
        void del_symbol_stream(
                const std::string &symbol,
                const uint32_t period) {
            auto it = index_interval_to_str.find(period);
            if(it == index_interval_to_str.end()) return;
            std::string s = to_lower_case(symbol);
            json j;
            j["method"] = "UNSUBSCRIBE";
            j["params"] = json::array();
            std::string param = s + "@kline_" + it->second;
            j["params"][0] = param;
            j["id"] = 1;
            {
                std::lock_guard<std::mutex> lock(list_subscriptions_mutex);
                /* имя символа ОБЯЗАТЕЛЬНО В НИЖНЕМ РЕГИСТРЕ! */
                auto it_symbol = list_subscriptions.find(s);
                if(it_symbol != list_subscriptions.end()) {
                    auto it_period = it_symbol->second.find(period);
                    if(it_period != it_symbol->second.end()) {
                        it_symbol->second.erase(period);
                    }
                }
            }
            if(!is_open) return;
            send(j.dump());
        }
#endif

        void start() {
            if(client_future.valid()) return;
            /* запустим соединение в отдельном потоке */
            client_future = std::async(std::launch::async,[&]() {
                while(!is_close_connection) {
                    try {
                        /* создадим соединение */;
                        client = std::make_shared<WssClient>(
                                point,
                                true,
                                std::string(),
                                std::string(),
                                std::string(sert_file));

                        /* читаем собщения, которые пришли */
                        client->on_message =
                                [&](std::shared_ptr<WssClient::Connection> connection,
                                std::shared_ptr<WssClient::InMessage> message) {
                            parser(message->string());
                            //std::cout << "on_message " << message->string() << std::endl;
                        };

                        client->on_open =
                            [&](std::shared_ptr<WssClient::Connection> connection) {
                            {
                                std::lock_guard<std::mutex> lock(save_connection_mutex);
                                save_connection = connection;
                            }
                            is_open = true;

                            /* вызываем функцию обратного вызова */
                            if(on_start != nullptr) on_start();

                            /* подписываемся на поток котировок */
                            std::list<std::string> list_symbol;
                            {
                                std::lock_guard<std::mutex> lock(list_subscriptions_mutex);
                                if(list_subscriptions.size() == 0) return;
                                for(auto &item_symbol : list_subscriptions) {
                                    list_symbol.push_back(item_symbol.first);
                                }
                            }
                            subscribe_symbols(std::vector<std::string>(list_symbol.begin(), list_symbol.end()));
                            std::cout << "binomo api: wss start" << std::endl;
                        };

                        client->on_close =
                                [&](std::shared_ptr<WssClient::Connection> /*connection*/,
                                int status, const std::string & /*reason*/) {
                            is_websocket_init = false;
                            is_open = false;
                            is_error = true;
                            {
                                std::lock_guard<std::mutex> lock(save_connection_mutex);
                                if(save_connection) save_connection.reset();
                            }
                            std::cerr
                                << "binomo api: "
                                << point
                                << " closed connection with status code " << status
                                << std::endl;
                        };

                        // See http://www.boost.org/doc/libs/1_55_0/doc/html/boost_asio/reference.html, Error Codes for error code meanings
                        client->on_error =
                                [&](std::shared_ptr<WssClient::Connection> /*connection*/,
                                const SimpleWeb::error_code &ec) {
                            is_websocket_init = false;
                            is_open = false;
                            is_error = true;

                            {
                                std::lock_guard<std::mutex> lock(save_connection_mutex);
                                if(save_connection) save_connection.reset();
                            }

                            std::cerr
                                << "binomo api: "
                                << point
                                << " wss error: " << ec
                                << std::endl;
                        };
                        client->start();
                        client.reset();
                    } catch (std::exception& e) {
                        is_websocket_init = false;
                        is_error = true;
                    }
                    catch (...) {
                        is_websocket_init = false;
                        is_error = true;
                    }
                    if(is_close_connection) break;
					const uint64_t RECONNECT_DELAY = 1000;
					std::this_thread::sleep_for(std::chrono::milliseconds(RECONNECT_DELAY));
                } // while
            });
        }
    };
}

#endif // BINOMO_CPP_API_WEBSOCKET_HPP_INCLUDED
