#include <iostream>
#include <fstream>
#include "binomo-cpp-api-http.hpp"

int main() {
    std::cout << "start binance cpp api http step 1 test!" << std::endl;
    int err = 0;
    binomo_api::BinomoApiHttp<> binomo_http_api;
    {
        std::string symbol = "BTCUSD";
        std::cout << "binomo http api test 1" << std::endl;
        std::vector<binomo_api::common::Candle> candles;
        err = binomo_http_api.get_historical_data(candles, symbol, xtime::SECONDS_IN_DAY, xtime::get_timestamp(11,9,2020,0,0,0), xtime::get_timestamp());
        std::cout << "err " << err << std::endl;
        for(size_t i = 0; i < candles.size(); ++i) {
            binomo_api::common::Candle candle = candles[i];
            std::cout
                << symbol
                //<< " o: " << candle.open
                << " c: " << candle.close
                << " h: " << candle.high
                << " l: " << candle.low
                << " v: " << candle.volume
                << " t: " << xtime::get_str_date_time(candle.timestamp)
                << std::endl;
        }
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(5000));
    {
        std::string symbol = "ZCRYIDX";
        std::cout << "binomo http api test 2" << std::endl;
        std::vector<binomo_api::common::Candle> candles;
        err = binomo_http_api.get_historical_data(candles, symbol, 60, xtime::get_timestamp(11,9,2020,0,0,0), xtime::get_timestamp());
        std::cout << "err " << err << std::endl;
        for(size_t i = 0; i < candles.size(); ++i) {
            binomo_api::common::Candle candle = candles[i];
            std::cout
                << symbol
                //<< " o: " << candle.open
                << " c: " << candle.close
                << " h: " << candle.high
                << " l: " << candle.low
                << " v: " << candle.volume
                << " t: " << xtime::get_str_date_time(candle.timestamp)
                << std::endl;
        }
    }
    /*
    {
        std::cout << "SApi test 2" << std::endl;
        std::vector<xquotes_common::Candle> candles;
        err = binance_http_sapi.get_historical_data(candles, symbol, 1, xtime::get_timestamp(1,1,2020),xtime::get_timestamp(10,1,2020));
        std::cout << "err " << err << std::endl;
        for(size_t i = 0; i < candles.size(); ++i) {
            xquotes_common::Candle candle = candles[i];
            std::cout
                << symbol
                //<< " o: " << candle.open
                << " c: " << candle.close
                << " h: " << candle.high
                << " l: " << candle.low
                << " v: " << candle.volume
                << " t: " << xtime::get_str_date_time(candle.timestamp)
                << std::endl;
        }
    }
    */
    return 0;
/*
    {
        std::cout << "FApi test 1" << std::endl;
        std::vector<xquotes_common::Candle> candles;
        err = binance_http_fapi.get_historical_data_single_request(candles, symbol, 1, 10);
        std::cout << "err " << err << std::endl;
        for(size_t i = 0; i < candles.size(); ++i) {
            xquotes_common::Candle candle = candles[i];
            std::cout
                << symbol
                //<< " o: " << candle.open
                << " c: " << candle.close
                << " h: " << candle.high
                << " l: " << candle.low
                << " v: " << candle.volume
                << " t: " << xtime::get_str_date_time(candle.timestamp)
                << std::endl;
        }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(5000));
    {
        std::cout << "FApi test 2" << std::endl;
        std::vector<xquotes_common::Candle> candles;
        err = binance_http_fapi.get_historical_data(candles, symbol, 1, xtime::get_timestamp(1,1,2020),xtime::get_timestamp(10,1,2020));
        std::cout << "err " << err << std::endl;
        for(size_t i = 0; i < candles.size(); ++i) {
            xquotes_common::Candle candle = candles[i];
            std::cout
                << symbol
                //<< " o: " << candle.open
                << " c: " << candle.close
                << " h: " << candle.high
                << " l: " << candle.low
                << " v: " << candle.volume
                << " t: " << xtime::get_str_date_time(candle.timestamp)
                << std::endl;
        }
    }
*/
    std::system("pause");
    return 0;
}
