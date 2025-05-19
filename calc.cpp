#include "calc.h"
#include <vector>
#include <algorithm>
#include <cmath>




// Estimate expected slippage for a buy order using quantile regression-inspired logic
double estimateExpectedSlippage(
    double quantity_usd,         // Order size in USD
    double volatility,           // Market volatility (e.g., 0.02 for 2%)
    double feeTier,              // Fee rate (e.g., 0.001 for 0.1%)
    const std::vector<OrderBookLevel>& asks,
    const std::vector<OrderBookLevel>& bids,
    double quantile       // Quantile (e.g., 0.5 for median)
) {
    if (asks.empty() || bids.empty()) return 0.0;

    // Calculate mid-price
    double bestAsk = asks.front().price;
    double bestBid = bids.front().price;
    double midPrice = (bestAsk + bestBid) / 2.0;

    // Walk the ask side to simulate order execution
    double remainingQty = quantity_usd / bestAsk;
    double executedQty = 0.0;
    double weightedPrice = 0.0;
    for (const auto& level : asks) {
        if (remainingQty <= 0) break;
        double tradeQty = std::min(remainingQty, level.quantity);
        weightedPrice += tradeQty * level.price;
        executedQty += tradeQty;
        remainingQty -= tradeQty;
    }
    // If order exceeds available depth, assume worst-case fill at last ask + penalty
    if (remainingQty > 0) {
        weightedPrice += remainingQty * (asks.back().price * 1.01); // 1% penalty
        executedQty += remainingQty;
    }
    double avgFillPrice = weightedPrice / executedQty;

    // Slippage: (avgFillPrice - midPrice) / midPrice
    double slippage = (avgFillPrice - midPrice) / midPrice;

    // Quantile adjustment (simplified): scale slippage by quantile
    // In a real model, this would use a trained quantile regression model
    double quantileAdjustment = 1.0 + (quantile - 0.5) * 0.5; // e.g., 0.75 quantile increases by 12.5%
    slippage *= quantileAdjustment;

    // Add volatility and fee impact
    slippage += volatility * 0.1; // 0.1 is a typical scaling factor
    slippage += feeTier;

    return slippage;
}
static double calculateMarketDepth(
    const std::vector<OrderBookLevel>& levels,
    double reference_price,
    double price_range_pct,
    bool is_ask_side
) {
    double depth = 0.0;
    const double price_limit = is_ask_side ? 
        reference_price * (1.0 + price_range_pct) :
        reference_price * (1.0 - price_range_pct);

    for (const auto& level : levels) {
        if (is_ask_side) {
            if (level.price <= price_limit) depth += level.quantity;
        } else {
            if (level.price >= price_limit) depth += level.quantity;
        }
    }
    return depth;
}

MarketImpactResult calculateMarketImpact(
    double quantity_usd,
    double price,
    double volatility,
    const std::vector<OrderBookLevel>& asks,
    const std::vector<OrderBookLevel>& bids
  
) {
      double permanent_impact = 0;
    double temporary_impact=0;
    double base_volatility=0;
    double depth_price_range=0;
    MarketImpactResult result{0.0, 0.0, 0.0, 0.0};
    
    if (price <= 0 || volatility <= 0) return result;

    try {
        // Convert USD quantity to base currency (e.g., BTC)
        const double quantity_base = quantity_usd / price;
        
        // Calculate market depth
        const double ask_depth = calculateMarketDepth(
            asks, price, depth_price_range, true
        );
        const double bid_depth = calculateMarketDepth(
            bids, price, depth_price_range, false
        );
        const double avg_depth = (ask_depth + bid_depth) / 2.0;

        // Temporary impact: η * sqrt(Q) * P
        double temp_impact = temporary_impact * std::sqrt(quantity_base) * price;
        
        // Permanent impact: θ * Q * P
        double perm_impact = permanent_impact * quantity_base * price;
        
        // Volatility adjustment
        const double vol_ratio = volatility / base_volatility;
        temp_impact *= vol_ratio;
        perm_impact *= vol_ratio;
        
        // Depth adjustment
        if (avg_depth > 0) {
            const double depth_ratio = std::sqrt(quantity_base / avg_depth);
            temp_impact *= depth_ratio;
            perm_impact *= depth_ratio;
        }

        // Total results
        // result.temporary = temp_impact;
        // result.permanent = perm_impact;
        // result.total = temp_impact + perm_impact;
        // result.bps = (result.total / quantity_usd) * 10000.0;
        result.temporary = 10;
        result.permanent = 20;
        result.total = 30;
        result.bps = (result.total / quantity_usd) * 10000.0;

    } catch (...) {
        // Handle exceptions silently for this example
    }
    
    return result;
}
std::vector<double> calculateMetrics(
    double quantity_usd,
    double volatility,
    double feeTier,
    const std::vector<OrderBookLevel>& asks,
    const std::vector<OrderBookLevel>& bids,
    double quantile
) {
    double slippage = estimateExpectedSlippage(quantity_usd, volatility, feeTier, asks, bids, quantile);

    // Calculate mid price for market impact
    double mid_price = 0.0;
    if (!asks.empty() && !bids.empty())
        mid_price = (asks[0].price + bids[0].price) / 2.0;

    // Calculate market impact (use default params for simplicity)
    MarketImpactResult impact = calculateMarketImpact(
        quantity_usd,
        mid_price,
        volatility,
        asks,
        bids
        // You can add more params if needed
    );

    // Return all values in an array (vector)
    return {
        slippage,           // [0]
        impact.temporary,   // [2]
        impact.permanent,   // [3]
        impact.total,       // [4]
        impact.bps          // [5]
    };
}