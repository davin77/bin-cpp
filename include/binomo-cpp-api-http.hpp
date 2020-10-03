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
#ifndef BINOMO_CPP_API_HTTP_HPP_INCLUDED
#define BINOMO_CPP_API_HTTP_HPP_INCLUDED

#include "binomo-cpp-api-common.hpp"
#include <curl/curl.h>
#include <gzip/decompress.hpp>
#include <nlohmann/json.hpp>
#include "xtime.hpp"
#include <thread>
#include <future>
#include <mutex>
#include <atomic>
#include <array>
#include <map>
//#include "utf8.h" // http://utfcpp.sourceforge.net/

namespace binomo_api {
    using json = nlohmann::json;

    /** \brief Класс API брокера Binomo для работы с https запросами
     */
	template<class CANDLE = common::Candle>
    class BinomoApiHttp {
    private:
        std::string sert_file = "curl-ca-bundle.crt";   /**< Файл сертификата */
        std::string cookie_file = "binomo.cookie";
        char error_buffer[CURL_ERROR_SIZE];
        static const int TIME_OUT = 60; 				/**< Время ожидания ответа сервера для разных запросов */

        /** \brief Класс для хранения Http заголовков
         */
        class HttpHeaders {
        private:
            struct curl_slist *http_headers = nullptr;
        public:

            HttpHeaders() {};

            HttpHeaders(std::vector<std::string> headers) {
                for(size_t i = 0; i < headers.size(); ++i) {
                    add_header(headers[i]);
                }
            };

            void add_header(const std::string &header) {
                http_headers = curl_slist_append(http_headers, header.c_str());
            }

            void add_header(const std::string &key, const std::string &val) {
                std::string header(key + ": " + val);
                http_headers = curl_slist_append(http_headers, header.c_str());
            }

            ~HttpHeaders() {
                if(http_headers != nullptr) {
                    curl_slist_free_all(http_headers);
                    http_headers = nullptr;
                }
            };

            inline struct curl_slist *get() {
                return http_headers;
            }
        };

        std::atomic<xtime::ftimestamp_t> offset_timestamp = ATOMIC_VAR_INIT(0);

    public:

        /** \brief Получить метку времени сервера
         * \return Метка времени сервера
         */
        inline xtime::ftimestamp_t get_server_ftimestamp() {
            return  xtime::get_ftimestamp() + offset_timestamp;
        }

        /** \brief Установить смещение метки времени
         * \param offset Смещение метки времени
         */
        inline void set_server_offset_timestamp(const double offset) {
            offset_timestamp = offset;
        }

    private:

        /* ограничение количества запросов в минуту */
        std::atomic<uint32_t> request_counter = ATOMIC_VAR_INIT(0);
        std::atomic<uint32_t> request_limit = ATOMIC_VAR_INIT(30);
        std::atomic<xtime::timestamp_t> request_timestamp = ATOMIC_VAR_INIT(0);

        void check_request_limit(const uint32_t weight = 1) {
            request_counter += weight;
            if(request_timestamp == 0) {
                request_timestamp = xtime::get_first_timestamp_minute();
                return;
            } else
            if(request_timestamp == xtime::get_first_timestamp_minute()) {
                if(request_counter >= request_limit) {
                    while(request_timestamp == xtime::get_first_timestamp_minute()) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    }
                    request_counter = 0;
                    request_timestamp = xtime::get_first_timestamp_minute();
                    return;
                }
            } else {
                request_timestamp = xtime::get_first_timestamp_minute();
            }
        }

        /** \brief Callback-функция для обработки ответа
         * Данная функция нужна для внутреннего использования
         */
        static int binomo_writer(char *data, size_t size, size_t nmemb, void *userdata) {
            int result = 0;
            std::string *buffer = (std::string*)userdata;
            if(buffer != NULL) {
                buffer->append(data, size * nmemb);
                result = size * nmemb;
            }
            return result;
        }

        /** \brief Парсер строки, состоящей из пары параметров
         *
         * \param value Строка
         * \param one Первое значение
         * \param two Второе значение
         */
        static void parse_pair(std::string value, std::string &one, std::string &two) {
            if(value.back() != ' ' || value.back() != '\n' || value.back() != ' ') value += " ";
            std::size_t start_pos = 0;
            while(true) {
                std::size_t found_beg = value.find_first_of(" \t\n", start_pos);
                if(found_beg != std::string::npos) {
                    std::size_t len = found_beg - start_pos;
                    if(len > 0) {
                        if(start_pos == 0) one = value.substr(start_pos, len);
                        else two = value.substr(start_pos, len);
                    }
                    start_pos = found_beg + 1;
                } else break;
            }
        }

        /** \brief Callback-функция для обработки HTTP Header ответа
         * Данный метод нужен, чтобы определить, какой тип сжатия данных используется (или сжатие не используется)
         * Данный метод нужен для внутреннего использования
         */
        static int binomo_header_callback(char *buffer, size_t size, size_t nitems, void *userdata) {
            size_t buffer_size = nitems * size;
            std::map<std::string,std::string> *headers = (std::map<std::string,std::string>*)userdata;
            std::string str_buffer(buffer, buffer_size);
            std::string key, val;
            parse_pair(str_buffer, key, val);
            headers->insert({key, val});
            return buffer_size;
        }

        /** \brief Часть парсинга HTML
         * Данный метод нужен для внутреннего использования
         */
        std::size_t get_string_fragment(
                const std::string &str,
                const std::string &div_beg,
                const std::string &div_end,
                std::string &out,
                std::size_t start_pos = 0) {
            std::size_t beg_pos = str.find(div_beg, start_pos);
            if(beg_pos != std::string::npos) {
                std::size_t end_pos = str.find(div_end, beg_pos + div_beg.size());
                if(end_pos != std::string::npos) {
                    out = str.substr(beg_pos + div_beg.size(), end_pos - beg_pos - div_beg.size());
                    return end_pos;
                } else return std::string::npos;
            } else return std::string::npos;
        }

        /** \brief Часть парсинга HTML
         * Данная метод нужен для внутреннего использования
         */
        std::size_t get_string_fragment(
                const std::string &str,
                const std::string &div_beg,
                std::string &out) {
            std::size_t beg_pos = str.find(div_beg, 0);
            if(beg_pos != std::string::npos) {
                out = str.substr(beg_pos + div_beg.size());
                return beg_pos;
            } else return std::string::npos;
        }

        enum class TypesRequest {
            REQUEST_GET = 0,
            REQUEST_POST = 1,
            REQUEST_PUT = 2,
            REQUEST_DELETE = 3,
			REQUEST_OPTIONS = 4,
        };

        /** \brief Инициализация CURL
         *
         * Данная метод является общей инициализацией для разного рода запросов
         * Данный метод нужен для внутреннего использования
         * \param url URL запроса
         * \param body Тело запроса
         * \param response Ответ сервера
         * \param http_headers Заголовки HTTP
         * \param timeout Таймаут
         * \param writer_callback Callback-функция для записи данных от сервера
         * \param header_callback Callback-функция для обработки заголовков ответа
         * \param is_use_cookie Использовать cookie файлы
         * \param is_clear_cookie Очистить cookie файлы
         * \param type_req Использовать POST, GET и прочие запросы
         * \return вернет указатель на CURL или NULL, если инициализация не удалась
         */
        CURL *init_curl(
                const std::string &url,
                const std::string &body,
                std::string &response,
                struct curl_slist *http_headers,
                const int timeout,
                int (*writer_callback)(char*, size_t, size_t, void*),
                int (*header_callback)(char*, size_t, size_t, void*),
                void *userdata,
                const bool is_use_cookie = false,
                const bool is_clear_cookie = false,
                const TypesRequest type_request = TypesRequest::REQUEST_GET) {
            CURL *curl = curl_easy_init();
            if(!curl) return NULL;
            curl_easy_setopt(curl, CURLOPT_CAINFO, sert_file.c_str());
            curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, error_buffer);
            curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
            //curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
            if(type_request == TypesRequest::REQUEST_POST) curl_easy_setopt(curl, CURLOPT_POST, 1L);
            else if(type_request == TypesRequest::REQUEST_GET) curl_easy_setopt(curl, CURLOPT_POST, 0);
            else if(type_request == TypesRequest::REQUEST_PUT) curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
            else if(type_request == TypesRequest::REQUEST_DELETE) curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
			else if(type_request == TypesRequest::REQUEST_OPTIONS) curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "OPTIONS");
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writer_callback);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
            curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout); // выход через N сек
            if(is_use_cookie) {
                if(is_clear_cookie) curl_easy_setopt(curl, CURLOPT_COOKIELIST, "ALL");
                else curl_easy_setopt(curl, CURLOPT_COOKIEFILE, cookie_file.c_str()); // запускаем cookie engine
                curl_easy_setopt(curl, CURLOPT_COOKIEJAR, cookie_file.c_str()); // запишем cookie после вызова curl_easy_cleanup
            }
            curl_easy_setopt(curl, CURLOPT_HEADERDATA, userdata);
            curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, header_callback);
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, http_headers);
            if (type_request == TypesRequest::REQUEST_POST ||
                type_request == TypesRequest::REQUEST_PUT ||
                type_request == TypesRequest::REQUEST_DELETE)
                curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
            //curl_easy_setopt(curl, CURLOPT_VERBOSE, true);
            return curl;
        }

        /** \brief Обработать ответ сервера
         * \param curl Указатель на структуру CURL
         * \param headers Заголовки, которые были приняты
         * \param buffer Буфер с ответом сервера
         * \param response Итоговый ответ, который будет возвращен
         * \return Код ошибки
         */
        int process_server_response(CURL *curl, std::map<std::string,std::string> &headers, std::string &buffer, std::string &response) {
            CURLcode result = curl_easy_perform(curl);
            long response_code = 0;
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);

            if(result == CURLE_OK) {
                if(headers.find("Content-Encoding:") != headers.end()) {
                    std::string content_encoding = headers["Content-Encoding:"];
                    if(content_encoding.find("gzip") != std::string::npos) {
                        if(buffer.size() == 0) return common::NO_ANSWER;
                        const char *compressed_pointer = buffer.data();
                        response = gzip::decompress(compressed_pointer, buffer.size());
                    } else
                    if(content_encoding.find("identity") != std::string::npos) {
                        response = buffer;
                    } else {
                        if(response_code != 200 && response_code != 204) return common::CURL_REQUEST_FAILED;
                        return common::CONTENT_ENCODING_NOT_SUPPORT;
                    }
                } else
                if(headers.find("content-encoding:") != headers.end()) {
                    std::string content_encoding = headers["content-encoding:"];
                    if(content_encoding.find("gzip") != std::string::npos) {
                        if(buffer.size() == 0) return common::NO_ANSWER;
                        const char *compressed_pointer = buffer.data();
                        response = gzip::decompress(compressed_pointer, buffer.size());
                    } else
                    if(content_encoding.find("identity") != std::string::npos) {
                        response = buffer;
                    } else {
                        if(response_code != 200 && response_code != 204) return common::CURL_REQUEST_FAILED;
                        return common::CONTENT_ENCODING_NOT_SUPPORT;
                    }
                } else {
                    response = buffer;
                    if(response_code != 200 && response_code != 204) return common::CURL_REQUEST_FAILED;
                }
                if(response_code != 200 && response_code != 204) return common::CURL_REQUEST_FAILED;
            }
            return result;
        }

        /** \brief POST запрос
         *
         * Данный метод нужен для внутреннего использования
         * \param url URL сообщения
         * \param body Тело сообщения
         * \param http_headers Заголовки
         * \param response Ответ
         * \param is_use_cookie Использовать cookie файлы
         * \param is_clear_cookie Очистить cookie
         * \param timeout Время ожидания ответа
         * \return код ошибки
         */
        int post_request(
                const std::string &url,
                const std::string &body,
                struct curl_slist *http_headers,
                std::string &response,
                const bool is_use_cookie = false,
                const bool is_clear_cookie = false,
                const int timeout = TIME_OUT) {
            std::map<std::string,std::string> headers;
            std::string buffer;
            CURL *curl = init_curl(
                url,
                body,
                buffer,
                http_headers,
                timeout,
                binomo_writer,
                binomo_header_callback,
                &headers,
                is_use_cookie,
                is_clear_cookie,
                TypesRequest::REQUEST_POST);

            if(curl == NULL) return common::CURL_CANNOT_BE_INIT;
            int err = process_server_response(curl, headers, buffer, response);
            curl_easy_cleanup(curl);
            return err;
        }

        /** \brief PUT запрос
         *
         * Данный метод нужен для внутреннего использования
         * \param url URL сообщения
         * \param body Тело сообщения
         * \param http_headers Заголовки
         * \param response Ответ
         * \param is_use_cookie Использовать cookie файлы
         * \param is_clear_cookie Очистить cookie
         * \param timeout Время ожидания ответа
         * \return код ошибки
         */
        int put_request(
                const std::string &url,
                const std::string &body,
                struct curl_slist *http_headers,
                std::string &response,
                const bool is_use_cookie = false,
                const bool is_clear_cookie = false,
                const int timeout = TIME_OUT) {
            std::map<std::string,std::string> headers;
            std::string buffer;
            CURL *curl = init_curl(
                url,
                body,
                buffer,
                http_headers,
                timeout,
                binomo_writer,
                binomo_header_callback,
                &headers,
                is_use_cookie,
                is_clear_cookie,
                TypesRequest::REQUEST_PUT);

            if(curl == NULL) return common::CURL_CANNOT_BE_INIT;
            int err = process_server_response(curl, headers, buffer, response);
            curl_easy_cleanup(curl);
            return err;
        }

        /** \brief DELETE запрос
         *
         * Данный метод нужен для внутреннего использования
         * \param url URL сообщения
         * \param body Тело сообщения
         * \param http_headers Заголовки
         * \param response Ответ
         * \param is_use_cookie Использовать cookie файлы
         * \param is_clear_cookie Очистить cookie
         * \param timeout Время ожидания ответа
         * \return код ошибки
         */
        int delete_request(
                const std::string &url,
                const std::string &body,
                struct curl_slist *http_headers,
                std::string &response,
                const bool is_use_cookie = false,
                const bool is_clear_cookie = false,
                const int timeout = TIME_OUT) {
            std::map<std::string,std::string> headers;
            std::string buffer;
            CURL *curl = init_curl(
                url,
                body,
                buffer,
                http_headers,
                timeout,
                binomo_writer,
                binomo_header_callback,
                &headers,
                is_use_cookie,
                is_clear_cookie,
                TypesRequest::REQUEST_DELETE);

            if(curl == NULL) return common::CURL_CANNOT_BE_INIT;
            int err = process_server_response(curl, headers, buffer, response);
            curl_easy_cleanup(curl);
            return err;
        }

        /** \brief GET запрос
         *
         * Данный метод нужен для внутреннего использования
         * \param url URL сообщения
         * \param body Тело сообщения
         * \param http_headers Заголовки
         * \param response Ответ
         * \param is_clear_cookie Очистить cookie
         * \param timeout Время ожидания ответа
         * \return код ошибки
         */
        int get_request(
                const std::string &url,
                const std::string &body,
                struct curl_slist *http_headers,
                std::string &response,
                const bool is_use_cookie = false,
                const bool is_clear_cookie = false,
                const int timeout = TIME_OUT) {
            //int content_encoding = 0;   // Тип кодирования сообщения
            std::map<std::string,std::string> headers;
            std::string buffer;
            CURL *curl = init_curl(
                url,
                body,
                buffer,
                http_headers,
                timeout,
                binomo_writer,
                binomo_header_callback,
                &headers,
                is_use_cookie,
                is_clear_cookie,
                TypesRequest::REQUEST_GET);

            if(curl == NULL) return common::CURL_CANNOT_BE_INIT;
            int err = process_server_response(curl, headers, buffer, response);
            curl_easy_cleanup(curl);
            return err;
        }

		/** \brief GET запрос
         *
         * Данный метод нужен для внутреннего использования
         * \param url URL сообщения
         * \param body Тело сообщения
         * \param http_headers Заголовки
         * \param response Ответ
         * \param is_clear_cookie Очистить cookie
         * \param timeout Время ожидания ответа
         * \return код ошибки
         */
        int options_request(
                const std::string &url,
                const std::string &body,
                struct curl_slist *http_headers,
                std::string &response,
                const bool is_use_cookie = false,
                const bool is_clear_cookie = false,
                const int timeout = TIME_OUT) {
            //int content_encoding = 0;   // Тип кодирования сообщения
            std::map<std::string,std::string> headers;
            std::string buffer;
            CURL *curl = init_curl(
                url,
                body,
                buffer,
                http_headers,
                timeout,
                binomo_writer,
                binomo_header_callback,
                &headers,
                is_use_cookie,
                is_clear_cookie,
                TypesRequest::REQUEST_OPTIONS);

            if(curl == NULL) return common::CURL_CANNOT_BE_INIT;
            int err = process_server_response(curl, headers, buffer, response);
            curl_easy_cleanup(curl);
            return err;
        }

        std::string to_lower_case(const std::string &s){
            std::string temp = s;
            std::transform(temp.begin(), temp.end(), temp.begin(), [](char ch) {
                return std::use_facet<std::ctype<char>>(std::locale()).tolower(ch);
            });
            return temp;
        }

        std::string to_upper_case(const std::string &s){
            std::string temp = s;
            std::transform(temp.begin(), temp.end(), temp.begin(), [](char ch) {
                return std::use_facet<std::ctype<char>>(std::locale()).toupper(ch);
            });
            return temp;
        }

        void parse_history(
                std::vector<CANDLE> &candles,
                std::string &response) {
            try {
                json j = json::parse(response);
				if(j["success"] != true) return;
				json j_data = j["data"];
                const size_t size_data = j_data.size();
                for(size_t i = 0; i < size_data; ++i) {
                    json j_canlde = j_data[i];
					CANDLE candle;
                    std::string str_iso = j_canlde["created_at"];
                    xtime::DateTime date_time;
					if(!xtime::convert_iso(str_iso, date_time)) continue;
                    candle.timestamp = date_time.get_timestamp();
                    candle.open = j_canlde["open"];//std::atof(std::string(j_canlde["open"]).c_str());
                    candle.high = j_canlde["high"];//std::atof(std::string(j_canlde["high"]).c_str());
                    candle.low = j_canlde["low"];//std::atof(std::string(j_canlde["low"]).c_str());
                    candle.close = j_canlde["close"];//std::atof(std::string(j_canlde["close"]).c_str());
                    candle.volume = 0;
                    candles.push_back(candle);
                }
            } catch(...) {}
        }

        void parse_history(
                std::map<xtime::timestamp_t, CANDLE> &candles,
                std::string &response) {
			try {
                json j = json::parse(response);
				if(j["success"] != true) return;
				json j_data = j["data"];
                const size_t size_data = j_data.size();
                for(size_t i = 0; i < size_data; ++i) {
                    json j_canlde = j_data[i];
					CANDLE candle;
					std::string str_iso = j_canlde["created_at"];
                    xtime::DateTime date_time;
					if(!xtime::convert_iso(str_iso, date_time)) continue;
                    candle.timestamp = date_time.get_timestamp();
                    candle.open = j_canlde["open"];//std::atof(std::string(j_canlde["open"]).c_str());
                    candle.high = j_canlde["high"];//std::atof(std::string(j_canlde["high"]).c_str());
                    candle.low = j_canlde["low"];//std::atof(std::string(j_canlde["low"]).c_str());
                    candle.close = j_canlde["close"];//std::atof(std::string(j_canlde["close"]).c_str());
                    candle.volume = 0;
                    candles[(uint64_t)candle.timestamp] = candle;
                }
            } catch(...) {}
        }

        int get_request_none_security(std::string &response, const std::string &url, const uint64_t weight = 1) {
            const std::string body;
            check_request_limit(weight);
            HttpHeaders http_headers({
                //"Host: api.binomo.com",
                "User-Agent: Mozilla/5.0 (Windows NT 6.3; Win64; x64; rv:80.0) Gecko/20100101 Firefox/80.0",
                "Accept: application/json, text/plain, */*",
                "Accept-Language: ru-RU,ru;q=0.8,en-US;q=0.5,en;q=0.3",
                "Accept-Encoding: gzip",
                "Device-Type: web",
                "Cache-Control: no-cache, no-store, must-revalidate",
                "User-Timezone: Europe/Moscow",
                "Origin: https://binomo.com",
                "Referer: https://binomo.com/trading",
                "Connection: keep-alive",
                "Content-Type: application/json"});
            int err = get_request(url, body, http_headers.get(), response, true, false);
            return err;
        }

# 		if(0)
        int get_request_with_signature(
                std::string &response,
                std::string &query_string,
                std::string &url,
                const uint64_t recv_window,
                const uint64_t weight = 1) {
            add_recv_window_and_timestamp(query_string, recv_window);
            std::string signature(hmac::get_hmac(secret_key, query_string, hmac::TypeHash::SHA256));
            url += query_string;
            url += "&signature=";
            url += signature;
            HttpHeaders http_headers({
                "Accept-Encoding: gzip",
                "Content-Type: application/json",
                std::string("X-MBX-APIKEY: " + api_key)});
            const std::string body;
            check_request_limit(weight);
            int err = get_request(url, body, http_headers.get(), response, false, false);
            if(err != OK) {
                try {
                    json j = json::parse(response);
                    return (int)j["code"];
                } catch(...) {
                    return err;
                }
            }
            return err;
        }
# 		endif

        int options_request_none_security(std::string &response, const std::string &url, const uint64_t weight = 1) {
            const std::string body;
            check_request_limit(weight);
            HttpHeaders http_headers({
                "accept: */*",
                "accept-encoding: gzip",
                "accept-language: ru-RU,ru;q=0.9,en-US;q=0.8,en;q=0.7",
                "access-control-request-headers: authorization-token,cache-control,content-type,device-id,device-type,user-timezone,version",
                "access-control-request-headers: authorization-token,cache-control,content-type,device-id,device-type,user-timezone,version",
                "Content-Type: application/json"});
            int err = options_request(url, body, http_headers.get(), response, true, false);
            return err;
        }

    public:

        /** \brief Получить исторические данные
         *
         * \param candles Массив баров
         * \param symbol Имя символа
         * \param period Период
         * \param start_date Начальная дата загрузки
         * \param stop_date Конечная дата загрузки
         * \return Код ошибки
         */
        int get_historical_data(
                std::vector<CANDLE> &candles,
                const std::string &symbol,
                const uint32_t period,
                xtime::timestamp_t start_date,
                xtime::timestamp_t stop_date) {
            std::string s = common::normalize_symbol_name(symbol);
			auto it = common::normalize_name_to_ric.find(s);
            if(it == common::normalize_name_to_ric.end()) return common::DATA_NOT_AVAILABLE;

			xtime::timestamp_t time_period = 0;
			switch(period) {
            case 1:
            case 5:
            case 15:
            case 30:
            case xtime::SECONDS_IN_MINUTE:
                start_date = xtime::get_first_timestamp_day(start_date);
                time_period = xtime::SECONDS_IN_DAY;
                break;
            case (5 * xtime::SECONDS_IN_MINUTE):
                start_date = xtime::get_first_timestamp_day(start_date);
                start_date = start_date - (start_date % (xtime::SECONDS_IN_DAY*4));
                time_period = xtime::SECONDS_IN_DAY*4;
                break;
            case (15 * xtime::SECONDS_IN_MINUTE):
            case (30 * xtime::SECONDS_IN_MINUTE):
                start_date = xtime::get_first_timestamp_day(start_date);
                start_date = start_date - (start_date % (xtime::SECONDS_IN_DAY*24));
                break;
            case xtime::SECONDS_IN_HOUR:
                start_date = xtime::get_first_timestamp_day(start_date);
                start_date = start_date - (start_date % (xtime::SECONDS_IN_DAY*48));
                break;
            case (3*xtime::SECONDS_IN_HOUR):
                start_date = xtime::get_first_timestamp_day(start_date);
                start_date = start_date - (start_date % (xtime::SECONDS_IN_DAY*96));
                break;
            case xtime::SECONDS_IN_DAY:
                start_date = xtime::get_first_timestamp_day(start_date);
                start_date = start_date - (start_date % (xtime::SECONDS_IN_DAY*1536));
                break;
            default:
                return common::DATA_NOT_AVAILABLE;
                break;
			}
			xtime::timestamp_t current_date = start_date;

			while(true) {
                // https://api.binomo.com/platform/candles/Z-CRY%2FIDX/2020-09-23T00:00:00/3600?locale=ru
                std::string url("https://api.binomo.com/platform/candles/");
                url += common::url_encode(it->second);
                url += "/";
                // 2020-08-06T00:00:00
                url += xtime::to_string("%YYYY-%MM-%DDT%hh:%mm:%ss", current_date);
                url += "/";
                url += std::to_string(period);
                url += "?locale=en";
                std::string response;
                int err = get_request_none_security(response, url);
                if(err != common::OK) return err;
                std::vector<CANDLE> temp;
                parse_history(temp, response);

                //if(temp.size() > 0) {
                //    std::cout << xtime::get_str_date_time(temp.front().timestamp) << std::endl;
                //    std::cout << xtime::get_str_date_time(temp.back().timestamp) << std::endl;
                //}

                candles.insert(candles.end(), temp.begin(), temp.end());
                if(candles.size() > 0) if(candles.back().timestamp >= stop_date) break;
                current_date += time_period;
                //if(temp.back().timestamp >= stop_date) return;
            }

            return common::OK;
        }

        /** \brief Конструктор класса Binance Api для http запросов
         * \param user_sert_file Файл сертификата
         * \param user_cookie_file Cookie файлы
         */
        BinomoApiHttp(
                const std::string &user_sert_file = "curl-ca-bundle.crt",
                const std::string &user_cookie_file = "binomo.cookie") {
            sert_file = user_sert_file;
            cookie_file = user_cookie_file;
            curl_global_init(CURL_GLOBAL_ALL);
        };

        ~BinomoApiHttp() {}
    };
}
#endif // BINOMO_CPP_API_HTTP_HPP_INCLUDED
