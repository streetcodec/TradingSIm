// calc.h
#ifndef CALC_H
#define CALC_H

#include <vector>

struct OrderBookLevel {
    double price;
    double quantity;
};

struct MarketImpactResult {
    double temporary;
    double permanent;
    double total;
    double bps;
};

std::vector<double> calculateMetrics(
    double quantity_usd,
    double volatility,
    double feeTier,
    const std::vector<OrderBookLevel>& asks,
    const std::vector<OrderBookLevel>& bids,
    double quantile = 0.5
);

#endif
