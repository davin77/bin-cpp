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
#ifndef BINOMO_BOT_HPP_INCLUDED
#define BINOMO_BOT_HPP_INCLUDED

#include "binomo-bot-settings.hpp"
#include "..\binomo-cpp-api-http.hpp"
#include "..\binomo-cpp-api-websocket.hpp"
#include "named-pipe-server.hpp"
#include "..\tools\binomo-cpp-api-mql-hst.hpp"

namespace binomo_bot {
    using json = nlohmann::json;

    class BinomoBot {
    private:
        std::shared_ptr<binomo_api::BinomoApiPriceStream<>> candlestick_streams;    /**< Поток котировок */
		std::mutex candlestick_streams_mutex;

        std::shared_ptr<binomo_api::BinomoApiHttp<>> binomo_http_api;
		std::mutex binomo_http_api_mutex;

        std::shared_ptr<SimpleNamedPipe::NamedPipeServer> pipe_server;
		std::mutex pipe_server_mutex;

        std::vector<std::shared_ptr<binomo_api::MqlHst<>>> mql_history;
		std::mutex mql_history_mutex;

        std::atomic<bool> is_pipe_server = ATOMIC_VAR_INIT(false);

        template <typename T>
        struct atomwrapper {
            std::atomic<T> _a;

            atomwrapper()
            :_a() {}

            atomwrapper(const std::atomic<T> &a)
            :_a(a.load()) {}

            atomwrapper(const atomwrapper &other)
            :_a(other._a.load()) {}

            atomwrapper &operator=(const atomwrapper &other) {
                _a.store(other._a.load());
                return _a.load();
            }

            atomwrapper &operator=(const bool other) {
                _a.store(other);
                //return this;
            }

            bool operator==(const bool other) {
                return (_a.load() == other);
            }

            bool operator!=(const bool other) {
                return (!(_a.load() == other));
            }
        };

        std::vector<atomwrapper<bool>> is_init_mql_history;
        std::vector<atomwrapper<bool>> is_once_mql_history;

        std::mutex request_future_mutex;
        std::vector<std::future<void>> request_future;
        std::atomic<bool> is_future_shutdown = ATOMIC_VAR_INIT(false);

        std::atomic<bool> is_error = ATOMIC_VAR_INIT(false);

        /** \brief Очистить список запросов
         */
        void clear_request_future() {
            std::lock_guard<std::mutex> lock(request_future_mutex);
            size_t index = 0;
            while(index < request_future.size()) {
                try {
                    if(request_future[index].valid()) {
                        std::future_status status = request_future[index].wait_for(std::chrono::milliseconds(0));
                        if(status == std::future_status::ready) {
                            request_future[index].get();
                            request_future.erase(request_future.begin() + index);
                            continue;
                        }
                    }
                }
                catch(const std::exception &e) {
                    std::cerr <<"Error: BinanceApi::clear_request_future(), what: " << e.what() << std::endl;
                }
                catch(...) {
                    std::cerr <<"Error: BinanceApi::clear_request_future()" << std::endl;
                }
                ++index;
            }
        }

    public:

        /** \brief Инициализация главных компонент API
         * \param settings Настройки API
         * \return Вернет true в случае успеха
         */
        bool init_main(Settings &settings) {
            std::lock_guard<std::mutex> lock(binomo_http_api_mutex);
			binomo_http_api = std::make_shared<binomo_api::BinomoApiHttp<>>(
                    settings.sert_file,
                    settings.cookie_file);
            return true;
        }

        bool init_candles_stream_mt4(Settings &settings) {
            {
				std::lock_guard<std::mutex> lock(binomo_http_api_mutex);
				if(!binomo_http_api) return false;
			}
            if(is_error) return false;


			{
				std::lock_guard<std::mutex> lock(candlestick_streams_mutex);
				candlestick_streams = std::make_shared<binomo_api::BinomoApiPriceStream<>>(settings.sert_file);
			}

            /* проверяем параметры символов */
            for(size_t i = 0; i < settings.symbols.size(); ++i) {
				std::string s = binomo_api::common::normalize_symbol_name(settings.symbols[i].first);
				auto it = binomo_api::common::normalize_name_to_ric.find(s);
				if(it == binomo_api::common::normalize_name_to_ric.end()) {
					std::cerr << "binomo bot: symbol " << settings.symbols[i].first << " does not exist!" << std::endl;
				}
				switch(settings.symbols[i].second) {
				case 1:
				case 5:
				case 15:
				case 30:
                    std::cerr << "binomo bot: period " << settings.symbols[i].second << " does not exist!" << std::endl;
					return false;
                    break;
				case xtime::SECONDS_IN_MINUTE:
				case (5 * xtime::SECONDS_IN_MINUTE):
				case (15 * xtime::SECONDS_IN_MINUTE):
				case (30 * xtime::SECONDS_IN_MINUTE):
				case xtime::SECONDS_IN_HOUR:
				case (3*xtime::SECONDS_IN_HOUR):
				case xtime::SECONDS_IN_DAY:
					continue;
				default:
					std::cerr << "binomo bot: period " << settings.symbols[i].second << " does not exist!" << std::endl;
					return false;
					break;
				}
            }

            /* узнаем точность символов */
            std::vector<uint32_t> precisions;
            for(size_t i = 0; i < settings.symbols.size(); ++i) {
                auto it = binomo_api::common::normalize_name_to_precision.find(settings.symbols[i].first);
				if(it == binomo_api::common::normalize_name_to_precision.end()) {
					std::cerr << "binomo bot: precision " << settings.symbols[i].second << " does not exist!" << std::endl;
					return false;
				}
				precisions.push_back(it->second);
				if(precisions.back() <= settings.max_precisions) {
                    std::cout
                        << "binomo bot: symbol " << settings.symbols[i].first
                        << " period " << settings.symbols[i].second
                        << " precision " << precisions.back()
                        << std::endl;
                } else {
                    std::cout
                        << "binomo bot: symbol " << settings.symbols[i].first
                        << " period " << settings.symbols[i].second
                        << " precision " << precisions.back()
                        << " fix precision " << settings.max_precisions
                        << std::endl;
                }
            }

            /* инициализируем исторические данные MQL */
            for(size_t i = 0; i < settings.symbols.size(); ++i) {
                std::string mql_symbol_name = settings.symbols[i].first + settings.symbol_hst_suffix;
                if(mql_symbol_name.size() >= 11) mql_symbol_name = mql_symbol_name.substr(0,11);
                mql_history.push_back(std::make_shared<binomo_api::MqlHst<>>(
                    mql_symbol_name,
                    settings.path,
                    settings.symbols[i].second/xtime::SECONDS_IN_MINUTE,
                    std::min(precisions[i], settings.max_precisions),
                    settings.timezone));
            }

            is_init_mql_history.resize(settings.symbols.size());
            is_once_mql_history.resize(settings.symbols.size());
            for(size_t i = 0; i < settings.symbols.size(); ++i) {
                is_init_mql_history.push_back(std::atomic<bool>(false));
                is_once_mql_history.push_back(std::atomic<bool>(false));
                //is_init_mql_history[i] = false;
                //is_once_mql_history[i] = false;
            }

            /* инициализируем callback функцию потока котировок */
            candlestick_streams->on_candle = [&](
                    const std::string &symbol,
                    const binomo_api::common::Candle &candle,
                    const uint32_t period,
                    const bool close_candle) {
                //std::cout << "--symbol " << symbol << std::endl;
                /* получаем размер массива исторических даных MQL */
                size_t mql_history_size = 0;
                {
                    std::lock_guard<std::mutex> lock(mql_history_mutex);
                    mql_history_size = mql_history.size();
                }

                if(mql_history_size != 0) {
                    for(size_t i = 0; i < mql_history_size; ++i) {
                        /* проверяем соответствие параметров символа */
                        if(settings.symbols[i].first != symbol || settings.symbols[i].second != period) continue;

                        /* проверяем наличие инициализации исторических данных */
                        if(is_init_mql_history[i] == false) continue;

                        /* проверяем, была ли только что загрузка исторических данных */
                        if(is_once_mql_history[i] == false) {
                            /* получаем последнюю метку времени исторических данных */
                            xtime::timestamp_t last_timestamp = 0;
                            {
                                std::lock_guard<std::mutex> lock(mql_history_mutex);
                                last_timestamp = mql_history[i]->get_last_timestamp();
                            }

                            /* проверяем, не успел ли прийти новый бар,
                             * пока мы загружали исторические данных
                             */
                            if(candle.timestamp > last_timestamp) {
                                /* добавляем пропущенные исторические данные */
                                const xtime::timestamp_t step_time = period * xtime::SECONDS_IN_MINUTE;
                                for(xtime::timestamp_t t = last_timestamp; t < candle.timestamp; t += step_time) {
                                    binomo_api::common::Candle old_candle = candlestick_streams->get_timestamp_candle(symbol, period, t);
                                    std::lock_guard<std::mutex> lock(mql_history_mutex);

                                    /* если бар есть в истории потока котировок, то загрузим данные. Иначе пропускаем */
                                    if(old_candle.close != 0 && old_candle.timestamp == t) {
                                        mql_history[i]->add_new_candle(old_candle);
                                        //if(symbol == "EURUSD(OTC)") std::cout << "add_new_candle " << xtime::get_str_date_time(old_candle.timestamp) << std::endl;
                                    }
                                }
                            }
                            is_once_mql_history[i] = true;
                        }

                        /* если параметры соответствуют, обновляем исторические данные */
                        if(close_candle) {
                            std::lock_guard<std::mutex> lock(mql_history_mutex);
                            mql_history[i]->add_new_candle(candle);
                            //if(symbol == "EURUSD(OTC)") std::cout << "add_new_candle " << xtime::get_str_date_time(candle.timestamp) << std::endl;
                        } else {
                            std::lock_guard<std::mutex> lock(mql_history_mutex);
                            mql_history[i]->update_candle(candle);
                            //if(symbol == "EURUSD(OTC)") std::cout << "update_candle " << xtime::get_str_date_time(candle.timestamp) << std::endl;
                        }
                    }
                }
#               if(0)
                /* выводим сообщение о символе */
                std::cout
                    << symbol
                    //<< " o: " << candle.open
                    << " c: " << candle.close
                    //<< " h: " << candle.high
                    //<< " l: " << candle.low
                    << " v: " << candle.volume
                    << " p: " << period
                    << " t: " << xtime::get_str_time(candle.timestamp)
                    << " cc: " << close_candle
                    << std::endl;
#               endif
            };

            /* инициализируем потоки котировок */
            candlestick_streams->add_candles_stream(settings.symbols);
            candlestick_streams->start();
            candlestick_streams->wait();

            /* загружаем исторические данные */
            for(size_t i = 0; i < settings.symbols.size(); ++i) {
                const xtime::timestamp_t stop_date = xtime::get_first_timestamp_minute();
                const xtime::timestamp_t start_date = stop_date - (settings.symbols[i].second * settings.candles);
                std::vector<binomo_api::common::Candle> candles;

                int err = binomo_http_api->get_historical_data(candles, settings.symbols[i].first, settings.symbols[i].second, start_date, stop_date);

                for(size_t c = 0; c < candles.size(); ++c) {
                    binomo_api::common::Candle candle = candles[c];
                    /* добавляем все новые бары, за исключением последнего,
                     * так как он может быть изменен за время загрузки истории
                     */
                    if(c < (candles.size() - 1)) mql_history[i]->add_new_candle(candle);
                    else mql_history[i]->update_candle(candle);
#               if(0)
                    std::cout
                        << settings.symbols[i].first
                        //<< " o: " << candle.open
                        << " c: " << candle.close
                        //<< " h: " << candle.high
                        //<< " l: " << candle.low
                        << " v: " << candle.volume
                        << " t: " << xtime::get_str_date_time(candle.timestamp)
                        << std::endl;
#               endif
                }

                /* ставим флаг инициализации исторических данных */
                is_init_mql_history[i] = true;

                std::string mql_symbol_name = settings.symbols[i].first + settings.symbol_hst_suffix;
                if(mql_symbol_name.size() >= 11) mql_symbol_name = mql_symbol_name.substr(0,11);
                std::cout << "binomo bot: " << settings.symbols[i].first << " initialized as " << mql_symbol_name << ", candles = " << candles.size() << ", error code = " << err << std::endl;
            }
            return true;
        }

        bool init_pipe_server(Settings &settings) {
            {
				std::lock_guard<std::mutex> lock(binomo_http_api_mutex);
				if(!binomo_http_api) return false;
			}
            if(is_error) return false;

            const size_t buffer_size = 1024*10;

			std::lock_guard<std::mutex> lock(pipe_server_mutex);
			pipe_server = std::make_shared<SimpleNamedPipe::NamedPipeServer>(
				settings.named_pipe,
				buffer_size);


            pipe_server->on_open = [&](SimpleNamedPipe::NamedPipeServer::Connection* connection) {
                binomo_api::common::PrintThread{} << "binomo bot: named pipe open, handle = " << connection->get_handle() << std::endl;
#if(0)
                /* отправляем баланс при первом подключении */
                if(user_data_streams) {
                    std::vector<BalanceSpec> balances = user_data_streams->get_all_balance();
                    for(size_t i = 0; i < balances.size(); ++i) {
                        pipe_server->send_all(
                            "{\"asset\":\"" +
                            balances[i].asset +
                            "\",\"wallet_balance\":" +
                            std::to_string(balances[i].wallet_balance) +
                            ",\"cross_wallet_balance\":" +
                            std::to_string(balances[i].cross_wallet_balance) +
                            "}");
                    }
                }
#endif
                /* отправляем состояние "подключено" */
                pipe_server->send_all("{\"connection\":1}");
            };

            pipe_server->on_message = [&,settings](SimpleNamedPipe::NamedPipeServer::Connection* connection, const std::string &in_message) {
                /* обрабатываем входящие сообщения */
                //std::cout << "message " << in_message << ", handle: " << connection->get_handle() << std::endl;
                /* парисм */
                try {

                }
                catch(...) {
                    binomo_api::common::PrintThread{} << "binomo bot: named pipe error, json::parse" << std::endl;
                }
            };

            pipe_server->on_close = [&](SimpleNamedPipe::NamedPipeServer::Connection* connection) {
                binomo_api::common::PrintThread{} << "binomo bot: named pipe close, handle = " << connection->get_handle() << std::endl;
            };

            pipe_server->on_error = [&](SimpleNamedPipe::NamedPipeServer::Connection* connection, const std::error_code &ec) {
                binomo_api::common::PrintThread{} << "binomo bot: named pipe error, handle = " << connection->get_handle() << ", what " << ec.value() << std::endl;
            };

            /* запускаем сервер */
            pipe_server->start();
            is_pipe_server = true;
            return true;
        }

#if(0)
        /** \brief Получить метку времени сервера
         * \return Метка времени сервера
         */
        inline xtime::ftimestamp_t get_server_ftimestamp() {
            if(cand) return binance_http_fapi->get_server_ftimestamp();
            return xtime::get_ftimestamp();
        }
#endif

        BinomoBot() {

        };

        ~BinomoBot() {
            if(is_error) return;
            is_future_shutdown = true;

            /* закрываем все потоки */
            {
                std::lock_guard<std::mutex> lock(request_future_mutex);
                for(size_t i = 0; i < request_future.size(); ++i) {
                    if(request_future[i].valid()) {
                        try {
                            request_future[i].wait();
                            request_future[i].get();
                        }
                        catch(const std::exception &e) {
                            std::cerr <<"Error: BinomoBot::~BinomoBot(), waht: request_future, exception: " << e.what() << std::endl;
                        }
                        catch(...) {
                            std::cerr <<"Error: BinomoBot::~BinomoBot(), waht: request_future" << std::endl;
                        }
                    }
                }
            }
        }
    };

}

#endif // BINOMO_BOT_HPP_INCLUDED
