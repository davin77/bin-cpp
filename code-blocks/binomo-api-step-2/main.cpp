#include <iostream>
#include "binomo-cpp-api-websocket.hpp"

using namespace std;

int main() {
    std::cout << "binomo test: start" << std::endl;
    binomo_api::BinomoApiPriceStream<> price_streams;

    price_streams.on_candle = [&](
            const std::string &symbol,
            const binomo_api::common::Candle &candle,
            const uint32_t period,
            const bool close_candle) {
        if(close_candle) std::cout << "close ";
        std::cout
            << symbol
            //<< " o: " << candle.open
            << " c: " << candle.close
            << " h: " << candle.high
            << " l: " << candle.low
            << " v: " << candle.volume
            << " p: " << period
            << " t: " << xtime::get_str_time(candle.timestamp)
            << std::endl;
    };

    price_streams.on_tick = [&](const binomo_api::common::StreamTick &tick) {
        std::cout
            << tick.symbol
            << " p: " << tick.price
            << " t: " << xtime::get_str_time(tick.timestamp)
            << std::endl;
    };

    price_streams.on_start = [&]{
        std::cout << "binomo test: wss start" << std::endl;
        //price_streams.subscribe_symbol({"BTCUSD", "EURUSD", "BTCLTC"});
        price_streams.add_candles_stream({{"BTCUSD", 60}, {"EURUSD", 60}, {"EURCAD(OTC)", 60}});
    };

    //price_streams.add_symbol_stream("BTCUSD", 1);
    price_streams.start();
    price_streams.wait();
    //candlestick_streams.add_symbol_stream("btcusdt", 1);
    //candlestick_streams.add_symbol_stream("ethusdt", 1);
    //std::this_thread::sleep_for(std::chrono::milliseconds(5000));
    //candlestick_streams.add_symbol_stream("ethusdt", 5);
    //std::this_thread::sleep_for(std::chrono::milliseconds(5000));
    //std::cout << "UNSUBSCRIBE" << std::endl;
    //candlestick_streams.del_symbol_stream("ethusdt", 5);
    std::system("pause");
    return EXIT_SUCCESS;
}
