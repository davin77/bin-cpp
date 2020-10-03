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
#ifndef BINOMO_CPP_API_COMMON_HPP_INCLUDED
#define BINOMO_CPP_API_COMMON_HPP_INCLUDED

#include <iostream>
#include <fstream>
#include <sstream>
#include <mutex>
#include <algorithm>
#include <nlohmann/json.hpp>
#include "tools/base36.h"
#include "xtime.hpp"

namespace binomo_api {
    namespace common {
        using json = nlohmann::json;

        /// Варианты состояния ошибок
        enum ErrorType {
            OK = 0,                             ///< Ошибки нет
            CURL_CANNOT_BE_INIT = -1,           ///< CURL не может быть инициализирован
            CONTENT_ENCODING_NOT_SUPPORT = -2,  ///< Тип кодирования контента не поддерживается
            PARSER_ERROR = -3,                  ///< Ошибка парсера ответа от сервера
            JSON_PARSER_ERROR = -4,             ///< Ошибка парсера JSON
            NO_ANSWER = -5,                     ///< Нет ответа
            DATA_NOT_AVAILABLE = -6,            ///< Данные не доступны
            CURL_REQUEST_FAILED = -7,           ///< Ошибка запроса на сервер. Любой код статуса, который не равен 200, будет возвращать эту ошибку
            LIMITING_NUMBER_REQUESTS = -8,      ///< Нарушение ограничения скорости запроса.
            IP_BLOCKED = -9,                    ///< IP-адрес был автоматически заблокирован для продолжения отправки запросов после получения 429 кодов.
            WAF_LIMIT = -10,                    ///< нарушении лимита WAF (брандмауэр веб-приложений).
            NO_RESPONSE_WAITING_PERIOD = -11,
            INVALID_PARAMETER = -12,
            NO_PRICE_STREAM_SUBSCRIPTION = -13,
        };

        /** \brief Класс для хранения бара
         */
        class Candle {
        public:
            double open;
            double high;
            double low;
            double close;
            double volume;
            xtime::timestamp_t timestamp;

            Candle() :
                open(0),
                high(0),
                low (0),
                close(0),
                volume(0),
                timestamp(0) {
            };

            Candle(
                    const double &new_open,
                    const double &new_high,
                    const double &new_low,
                    const double &new_close,
                    const uint64_t &new_timestamp) :
                open(new_open),
                high(new_high),
                low (new_low),
                close(new_close),
                volume(0),
                timestamp(new_timestamp) {
            }

            Candle(
                    const double &new_open,
                    const double &new_high,
                    const double &new_low,
                    const double &new_close,
                    const double &new_volume,
                    const uint64_t &new_timestamp) :
                open(new_open),
                high(new_high),
                low (new_low),
                close(new_close),
                volume(new_volume),
                timestamp(new_timestamp) {
            }
        };

        /** \brief Класс для хранения данных тика
         */
        class StreamTick {
        public:
            std::string symbol;
            double price = 0;
            xtime::ftimestamp_t timestamp = 0;
            uint32_t precision = 0;
            StreamTick() {};
        };

        /** \brief Параметры символов
         */
        class SymbolConfig {
        public:
            bool is_active = false;
            uint32_t precision = 0;

            SymbolConfig() {};
        };

		const std::map<std::string, std::string> name_to_ric =
		{
			{"Crypto IDX"	,"Z-CRY/IDX"	},
			{"AUD/NZD"		,"AUD/NZD-HSFX" },
			{"GBP/NZD"		,"GBP/NZD-HSFX" },
			{"EUR/NZD"		,"EUR/NZD-HSFX" },
			{"EUR/MXN"		,"EUR/MXN-HSFX" },
			{"EUR IDX"		,"EURX-DXF"     },
			{"JPY IDX"		,"JPYX-DXF"     },
			{"EUR/USD"		,"EURO"         },
			{"CRY IDX"		,"CRY/IDX"      },
			{"BTC/LTC"		,"BTC/LTC"      },
			{"AUD/USD"		,"AUD/USD"      },
			{"AUD/CAD"		,"AUD/CAD"      },
			{"EUR/JPY"		,"EUR/JPY"      },
			{"AUD/JPY"		,"AUD/JPY"      },
			{"USD/JPY"		,"USD/JPY"      },
			{"USD/CAD"		,"USD/CAD"      },
			{"EUR/CAD (OTC)"	,"Z-EUR/CAD"},
			{"NZD/USD"		,"NZD/USD"      },
			{"GBP/USD"		,"GBP/USD"      },
			{"Gold"			,"XAU/USD-HSFX" },
			{"USD/JPY (OTC)"	,"Z-USD/JPY"},
			{"GBP/USD (OTC)"	,"Z-GBP/USD"},
			{"EUR/USD (OTC)"	,"Z-EUR/USD"},
			{"USD/CHF"		,"USD/CHF"      },
			{"AUD/CAD (OTC)"	,"Z-AUD/CAD"},
			{"Bitcoin"		,"BTC/USD"      },
			{"GBP/JPY (OTC)"	,"Z-GBP/JPY"},
			{"CHF/JPY"		,"CHF/JPY"      },
			{"NZD/JPY"		,"NZD/JPY"      }
		};

		const std::map<std::string, std::string> normalize_name_to_ric =
		{
			{"ZCRYIDX"		,"Z-CRY/IDX"	},
			{"AUDNZD"		,"AUD/NZD-HSFX" },
			{"GBPNZD"		,"GBP/NZD-HSFX" },
			{"EURNZD"		,"EUR/NZD-HSFX" },
			{"EURMXN"		,"EUR/MXN-HSFX" },
			{"EURIDX"		,"EURX-DXF"     },
			{"JPYIDX"		,"JPYX-DXF"     },
			{"EURUSD"		,"EURO"         },
			{"CRYIDX"		,"CRY/IDX"      },
			{"BTCLTC"		,"BTC/LTC"      },
			{"AUDUSD"		,"AUD/USD"      },
			{"AUDCAD"		,"AUD/CAD"      },
			{"EURJPY"		,"EUR/JPY"      },
			{"AUDJPY"		,"AUD/JPY"      },
			{"USDJPY"		,"USD/JPY"      },
			{"USDCAD"		,"USD/CAD"      },
			{"EURCAD(OTC)"	,"Z-EUR/CAD"},
			{"NZDUSD"		,"NZD/USD"      },
			{"GBPUSD"		,"GBP/USD"      },
			{"XAUUSD"		,"XAU/USD-HSFX" },
			{"USDJPY(OTC)"	,"Z-USD/JPY"	},
			{"GBPUSD(OTC)"	,"Z-GBP/USD"	},
			{"EURUSD(OTC)"	,"Z-EUR/USD"	},
			{"USDCHF"		,"USD/CHF"      },
			{"AUDCAD(OTC)"	,"Z-AUD/CAD"	},
			{"BTCUSD"		,"BTC/USD"      },
			{"GBPJPY(OTC)"	,"Z-GBP/JPY"	},
			{"CHFJPY"		,"CHF/JPY"      },
			{"NZDJPY"		,"NZD/JPY"      }
		};

		const std::map<std::string, uint32_t> normalize_name_to_precision =
		{
			{"ZCRYIDX"		,9 },
			{"AUDNZD"		,6 },
			{"GBPNZD"		,6 },
			{"EURNZD"		,6 },
			{"EURMXN"		,6 },
			{"EURIDX"		,8 },
			{"JPYIDX"		,6 },
			{"EURUSD"		,6 },
			{"CRYIDX"		,6 },
			{"BTCLTC"		,7 },
			{"AUDUSD"		,6 },
			{"AUDCAD"		,6 },
			{"EURJPY"		,4 },
			{"AUDJPY"		,4 },
			{"USDJPY"		,4 },
			{"USDCAD"		,6 },
			{"EURCAD(OTC)"	,6 },
			{"NZDUSD"		,6 },
			{"GBPUSD"		,6 },
			{"XAUUSD"		,6 },
			{"USDJPY(OTC)"	,4 },
			{"GBPUSD(OTC)"	,6 },
			{"EURUSD(OTC)"	,6 },
			{"USDCHF"		,6 },
			{"AUDCAD(OTC)"	,6 },
			{"BTCUSD"		,5 },
			{"GBPJPY(OTC)"	,4 },
			{"CHFJPY"		,4 },
			{"NZDJPY"		,4 }
		};


		const std::map<std::string, std::string> ric_to_normalize_name =
		{
            {"Z-CRY/IDX"	,"ZCRYIDX"		},
            {"AUD/NZD-HSFX" ,"AUDNZD"		},
            {"GBP/NZD-HSFX" ,"GBPNZD"		},
            {"EUR/NZD-HSFX" ,"EURNZD"		},
            {"EUR/MXN-HSFX" ,"EURMXN"		},
            {"EURX-DXF"     ,"EURIDX"		},
            {"JPYX-DXF"     ,"JPYIDX"		},
            {"EURO"         ,"EURUSD"		},
            {"CRY/IDX"      ,"CRYIDX"		},
            {"BTC/LTC"      ,"BTCLTC"		},
            {"AUD/USD"      ,"AUDUSD"		},
            {"AUD/CAD"      ,"AUDCAD"		},
            {"EUR/JPY"      ,"EURJPY"		},
            {"AUD/JPY"      ,"AUDJPY"		},
            {"USD/JPY"      ,"USDJPY"		},
            {"USD/CAD"      ,"USDCAD"		},
            {"Z-EUR/CAD"	,"EURCAD(OTC)"	},
            {"NZD/USD"      ,"NZDUSD"		},
            {"GBP/USD"      ,"GBPUSD"		},
            {"XAU/USD-HSFX" ,"XAUUSD"		},
            {"Z-USD/JPY"	,"USDJPY(OTC)"	},
            {"Z-GBP/USD"	,"GBPUSD(OTC)"	},
            {"Z-EUR/USD"	,"EURUSD(OTC)"	},
            {"USD/CHF"      ,"USDCHF"		},
            {"Z-AUD/CAD"	,"AUDCAD(OTC)"	},
            {"BTC/USD"      ,"BTCUSD"		},
            {"Z-GBP/JPY"	,"GBPJPY(OTC)"	},
            {"CHF/JPY"      ,"CHFJPY"		},
            {"NZD/JPY"      ,"NZDJPY"		}
        };

        /** \brief Открыть файл JSON
         *
         * Данная функция прочитает файл с JSON и запишет данные в JSON структуру
         * \param file_name Имя файла
         * \param auth_json Структура JSON с данными из файла
         * \return Вернет true в случае успешного завершения
         */
        bool open_json_file(const std::string &file_name, json &auth_json) {
            std::ifstream auth_file(file_name);
            if(!auth_file) {
                std::cerr << "open file " << file_name << " error" << std::endl;
                return false;
            }
            try {
                auth_file >> auth_json;
            }
            catch (json::parse_error &e) {
                std::cerr << "json parser error: " << std::string(e.what()) << std::endl;
                auth_file.close();
                return false;
            }
            catch (std::exception e) {
                std::cerr << "json parser error: " << std::string(e.what()) << std::endl;
                auth_file.close();
                return false;
            }
            catch(...) {
                std::cerr << "json parser error" << std::endl;
                auth_file.close();
                return false;
            }
            auth_file.close();
            return true;
        }

        /** \brief Обработать аргументы
         *
         * Данная функция обрабатывает аргументы от командной строки, возвращая
         * результат как пара ключ - значение.
         * \param argc количество аргуметов
         * \param argv вектор аргументов
         * \param f лябмда-функция для обработки аргументов командной строки
         * \return Вернет true если ошибок нет
         */
        bool process_arguments(
            const int argc,
            char **argv,
            std::function<void(
                const std::string &key,
                const std::string &value)> f) noexcept {
            if(argc <= 1) return false;
            bool is_error = true;
            for(int i = 1; i < argc; ++i) {
                std::string key = std::string(argv[i]);
                if(key.size() > 0 && (key[0] == '-' || key[0] == '/')) {
                    uint32_t delim_offset = 0;
                    if(key.size() > 2 && (key.substr(2) == "--") == 0) delim_offset = 1;
                    std::string value;
                    if((i + 1) < argc) value = std::string(argv[i + 1]);
                    is_error = false;
                    if(f != nullptr) f(key.substr(delim_offset), value);
                }
            }
            return !is_error;
        }

        class PrintThread: public std::ostringstream {
        private:
            static inline std::mutex _mutexPrint;

        public:
            PrintThread() = default;

            ~PrintThread() {
                std::lock_guard<std::mutex> guard(_mutexPrint);
                std::cout << this->str();
            }
        };

        std::string to_upper_case(const std::string &s){
            std::string temp = s;
            std::transform(temp.begin(), temp.end(), temp.begin(), [](char ch) {
                return std::use_facet<std::ctype<char>>(std::locale()).toupper(ch);
            });
            return temp;
        }

        std::string to_lower_case(const std::string &s){
            std::string temp = s;
            std::transform(temp.begin(), temp.end(), temp.begin(), [](char ch) {
                return std::use_facet<std::ctype<char>>(std::locale()).tolower(ch);
            });
            return temp;
        }

		std::string normalize_symbol_name(std::string symbol_name) {
            while(true) {
                auto it_str = symbol_name.find('/');
                if(it_str != std::string::npos) symbol_name.erase(it_str, 1);
                else break;
            }
            while(true) {
                auto it_str = symbol_name.find('-');
                if(it_str != std::string::npos) symbol_name.erase(it_str, 1);
                else break;
            }
			while(true) {
                auto it_str = symbol_name.find(' ');
                if(it_str != std::string::npos) symbol_name.erase(it_str, 1);
                else break;
            }
            return to_upper_case(symbol_name);
        }

        inline std::string get_uuid() {
            uint64_t timestamp1000 = xtime::get_ftimestamp() * 1000.0 + 0.5;
            std::string temp(CBase36::encodeInt(timestamp1000));
            temp += CBase36::randomString(10,11);
            std::transform(temp.begin(), temp.end(),temp.begin(), ::toupper);
            return temp;
        }

        inline std::string get_uuid(const double ftimestamp) {
            uint64_t timestamp1000 = ftimestamp * 1000.0 + 0.5;
            std::string temp(CBase36::encodeInt(timestamp1000));
            temp += CBase36::randomString(10,11);
            std::transform(temp.begin(), temp.end(),temp.begin(), ::toupper);
            return temp;
        }

        std::string url_encode(std::string str){
            std::string new_str = "";
            char c;
            int ic;
            const char* chars = str.c_str();
            char bufHex[10];
            int len = strlen(chars);

            for(int i=0;i<len;i++){
                c = chars[i];
                ic = c;
                // uncomment this if you want to encode spaces with +
                /*if (c==' ') new_str += '+';
                else */if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') new_str += c;
                else {
                    sprintf(bufHex,"%X",c);
                    if(ic < 16)
                        new_str += "%0";
                    else
                        new_str += "%";
                    new_str += bufHex;
                }
            }
            return new_str;
         }
    }
}

#endif // BINOMO_CPP_API_COMMON_HPP_INCLUDED
