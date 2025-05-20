#include "calc.h"
#include <vector>
#include <algorithm>
#include <cmath>
#include <map>
#include <iostream>


// Estimate expected slippage for a buy order using quantile regression-inspired logic
double estimateExpectedSlippage(
    double quantity_usd,         // Order size in USD (e.g., $100,000)
    double volatility,           // Market volatility (e.g., 0.02 for 2%)
    double feeTier,              // Fee rate (e.g., 0.001 for 0.1%)
    const std::vector<OrderBookLevel>& asks,
    const std::vector<OrderBookLevel>& bids,
    double k_vol = 0.1           // Volatility adjustment coefficient (default: 0.1)
) {
    if (asks.empty() || bids.empty()) return 0.0;

    // Calculate mid-price
    double bestAsk = asks.front().price;
    double bestBid = bids.front().price;
    double midPrice = (bestAsk + bestBid) / 2.0;

    // Simulate order execution (walk the order book)
    double remainingQty = quantity_usd / bestAsk; // Convert USD to shares/units
    double executedQty = 0.0;
    double weightedPrice = 0.0;

    for (const auto& level : asks) {
        if (remainingQty <= 0) break;
        double tradeQty = std::min(remainingQty, level.quantity);
        weightedPrice += tradeQty * level.price;
        executedQty += tradeQty;
        remainingQty -= tradeQty;
    }

    // If order exceeds available depth, apply penalty (1% worse than last ask)
    if (remainingQty > 0) {
        weightedPrice += remainingQty * (asks.back().price * 1.01);
        executedQty += remainingQty;
    }

    // Calculate average fill price
    double avgFillPrice = weightedPrice / executedQty;

    // Base slippage: (avgFillPrice - midPrice) / midPrice
    double baseSlippage = (avgFillPrice - midPrice) / midPrice;

    // Volatility adjustment: volatility * k_vol
    double volatilityAdjustment = volatility * k_vol;

    // Total slippage = base slippage + volatility adjustment
    double totalSlippage = baseSlippage + volatilityAdjustment;

    // Debug prints (optional)
    std::cout << "Mid Price: " << midPrice << std::endl;
    std::cout << "Avg Fill Price: " << avgFillPrice << std::endl;
    std::cout << "Base Slippage: " << baseSlippage * 100 << "%" << std::endl;
    std::cout << "Volatility Adjustment: " << volatilityAdjustment * 100 << "%" << std::endl;
    std::cout << "Total Slippage: " << totalSlippage * 100 << "%" << std::endl;

    return totalSlippage;
}

double calculateMarketImpact(
   double quantity_usd,            // Order size in USD
    double volatility,              // Annualized volatility (0.3 for 30%)
    const std::vector<OrderBookLevel>& asks,  // Ask levels (ascending price)
    const std::vector<OrderBookLevel>& bids,  // Bid levels (descending price)
    double slippage_pct,            // Slippage as percentage (e.g., 0.1 for 0.1%)
    double permanent_impact = 2.5e-6,  // θ (permanent impact parameter)
    double temporary_impact = 2.5e-6   // η (temporary impact parameter)
) {
    // Validate inputs
    if (quantity_usd <= 0 || volatility <= 0 || asks.empty() || bids.empty()) {
        return 0.0;
    }

    // Calculate market prices from order book
    double bestAsk = asks.front().price;
    double bestBid = bids.front().price;
    double midPrice = (bestAsk + bestBid) / 2.0;

    // Convert USD quantity to base asset quantity using mid price
    const double quantity_base = quantity_usd / midPrice;

    // Calculate market depth in one pass
    double cumulative_quantity = 0.0;
    double cumulative_value = 0.0;
    for (const auto& level : asks) {
        if (cumulative_quantity >= quantity_base) break;
        double taken = std::min(level.quantity, quantity_base - cumulative_quantity);
        cumulative_quantity += taken;
        cumulative_value += taken * level.price;
    }
    const double market_depth = (cumulative_quantity > 0) ? 
                              (cumulative_value / asks[0].price) : 0.0;

    // Calculate Almgren-Chriss impact components
    const double order_value = quantity_base * midPrice;
    
    // Temporary impact: η * sqrt(quantity) * midPrice
    double temp_impact = temporary_impact * std::sqrt(quantity_base) * midPrice;
    
    // Permanent impact: θ * quantity * midPrice
    double perm_impact = permanent_impact * quantity_base * midPrice;
    
    // Adjust impacts based on volatility (assuming base volatility of 0.3)
    const double vol_adjustment = volatility / 0.3;
    temp_impact *= vol_adjustment;
    perm_impact *= vol_adjustment;
    
    // Adjust for market depth if available
    if (market_depth > 0) {
        const double depth_adj = std::sqrt(quantity_base / market_depth);
        temp_impact *= depth_adj;
        perm_impact *= depth_adj;
    }
    
    const double ac_impact = temp_impact + perm_impact;

    // Convert percentage slippage to absolute value
    const double slippage_impact = (slippage_pct / 100.0) * order_value;

    // Combine impacts (weighted average)
    const double total_impact_value = (ac_impact + slippage_impact) / 2.0;

    // Return as percentage of order value (0.01 = 1%)
    std::cout<<"total_impact_value / order_value  "<<total_impact_value / order_value<<"\n";
    return total_impact_value / order_value;
}
double calculateFees(
  int fee_tier,
    double quantity,
    const std::vector<OrderBookLevel>& asks
    )
{
    // Fee tiers map - taker fee only (since it's always taker for market orders)
    static const std::map<int, double> fee_tiers = {
        {0, 0.0010},  // VIP 0
        {1, 0.0008},  // VIP 1
        {2, 0.0007},  // VIP 2
        {3, 0.0006},  // VIP 3
        {4, 0.0005},  // VIP 4
        {5, 0.0004}   // VIP 5
    };
    
    // Find the fee tier
    auto it = fee_tiers.find(fee_tier);
    if (it == fee_tiers.end()) {
        throw std::invalid_argument("Invalid fee tier (must be 0-5)");
    }
    
    double taker_fee = it->second;
    double remaining_qty = quantity;
    double total_fees = 0.0;
    
    // Calculate fees by consuming the ask side of the order book
    for (const auto& level : asks) {
        if (remaining_qty <= 0) break;
        
        double qty = std::min(remaining_qty, level.quantity);
        total_fees += qty * level.price * taker_fee;
        remaining_qty -= qty;
    }
    
    if (remaining_qty > 0) {
        throw std::runtime_error("Not enough liquidity in the order book to fulfill the order");
    }
    
    return total_fees;
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


    // Calculate market impact (use default params for        mid_price, simplicity)
    double impact = calculateMarketImpact(
        quantity_usd,
        volatility,
        asks,
        bids,
        slippage
    );
    double fees = calculateFees(
        feeTier,
        quantity_usd,
        asks
    );

    // Return all values in an array (vector)
    return {
        slippage,           // [0]
        impact,
        fees       // [5]
    };
}