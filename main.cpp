#include <wx/wx.h>
#include <wx/thread.h>
#include <atomic>
#include <mutex>
#include <string>
#include <chrono>
#include <memory>
#include <ixwebsocket/IXWebSocket.h>
#include <rapidjson/document.h>
#include "calc.h"

// Custom event for spread updates
wxDECLARE_EVENT(EVT_METRIC_UPDATE, wxCommandEvent);
wxDEFINE_EVENT(EVT_METRIC_UPDATE, wxCommandEvent);

struct TradingParams {
    wxString exchange = "OKX";
    wxString asset = "BTC-USDT-SWAP";
    wxString order_type = "market";
    wxString quantity = "500.0";
    wxString volatility = "0.02";
    wxString fee_tier = "1"; // 0.1% in decimal
    wxString current_endpoint;
    std::atomic<bool> needs_reconnect{false};
    std::mutex mutex;
};

class WebSocketThread : public wxThread {
private:
    wxEvtHandler* m_handler;
    TradingParams* m_params;
    
public:
    WebSocketThread(wxEvtHandler* handler, TradingParams* params)
        : wxThread(wxTHREAD_DETACHED), m_handler(handler), m_params(params) {}

protected:
    virtual ExitCode Entry() override {
        ix::WebSocket webSocket;
        while (!TestDestroy()) {
            {
                std::lock_guard<std::mutex> lock(m_params->mutex);
                m_params->current_endpoint = "wss://ws.gomarket-cpp.goquant.io/ws/l2-orderbook/okx/" + m_params->asset;
                webSocket.setUrl(m_params->current_endpoint.ToStdString());
                m_params->needs_reconnect = false;
            }

            webSocket.setOnMessageCallback([this](const ix::WebSocketMessagePtr& msg) {
                if (msg->type == ix::WebSocketMessageType::Message) {
                    rapidjson::Document doc;
                    doc.Parse(msg->str.c_str());
                    std::cout<<msg->str<<std::endl;
                    
                    if (!doc.HasParseError() && doc.HasMember("asks") && doc.HasMember("bids")) {
                        const auto& asks_json = doc["asks"];
                        const auto& bids_json = doc["bids"];
                        
                        if (asks_json.Size() > 0 && bids_json.Size() > 0) {
                            std::vector<OrderBookLevel> asks, bids;
                           
                            // Parse ask levels
                            for (rapidjson::SizeType i = 0; i < asks_json.Size(); i++) {
                                if (asks_json[i].Size() >= 2) {
                                    asks.push_back({
                                        std::stod(asks_json[i][0].GetString()),
                                        std::stod(asks_json[i][1].GetString())
                                    });
                                }
                            }
                            
                            // Parse bid levels
                            for (rapidjson::SizeType i = 0; i < bids_json.Size(); i++) {
                                if (bids_json[i].Size() >= 2) {
                                    bids.push_back({
                                        std::stod(bids_json[i][0].GetString()),
                                        std::stod(bids_json[i][1].GetString())
                                    });
                                }
                            }
                            
                            if (!asks.empty() && !bids.empty()) {
                                // Get current parameters
                                double quantity = 0.0, volatility = 0.0, fee_tier = 0.0;
                                try {
                                    std::lock_guard<std::mutex> lock(m_params->mutex);
                                    quantity = std::stod(m_params->quantity.ToStdString());
                                    volatility = std::stod(m_params->volatility.ToStdString());
                                    fee_tier = std::stod(m_params->fee_tier.ToStdString());
                                } catch (const std::exception& e) {
                                    wxString errorText = wxString::Format("Error parsing parameters: %s", e.what());
                                    wxCommandEvent event(EVT_METRIC_UPDATE);
                                    event.SetString(errorText);
                                    wxQueueEvent(m_handler, event.Clone());
                                    // continue;
                                }

                                auto start_time = std::chrono::high_resolution_clock::now();
                                
                                // Calculate metrics with error handling
                                std::vector<double> metrics;
                                try {
                                    metrics = calculateMetrics(
                                        quantity,
                                        volatility,
                                        fee_tier,
                                        asks,
                                        bids
                                    );
                                } catch (const std::exception& e) {
                                    wxString errorText = wxString::Format("Error calculating metrics: %s", e.what());
                                    wxCommandEvent event(EVT_METRIC_UPDATE);
                                    event.SetString(errorText);
                                    wxQueueEvent(m_handler, event.Clone());
                                    // continue;
                                }

                                auto calc_end_time = std::chrono::high_resolution_clock::now();
                                auto calc_latency_us = std::chrono::duration_cast<std::chrono::microseconds>(calc_end_time - start_time).count();
                                
                                // Format the display text with proper error checking
                                wxString displayText;
                                try {
                                    displayText = wxString::Format(
                                        "Slippage: %.4f%%\n"
                                        "Impact Cost: %.4f%%\n"
                                        "Fees: %.4f%%\n"
                                        "Net Cost: %.4f%%\n"
                                        "Maker/Taker Ratio: %.2f\n"
                                        "Calculation Time: %.2f ms\n"
                                        "UI Update Time: %.2f ms",
                                        metrics[0] * 100.0,   // slippage in %
                                        metrics[1] * 100.0,   // impact in %
                                        metrics[2] * 100.0,   // fees in %
                                        metrics[3],          // net cost in %
                                        metrics[4],          // maker/taker proportion
                                        calc_latency_us / 1000.0,  // calculation time in ms
                                        0.0  // UI update time will be set after update
                                    );
                                } catch (const std::exception& e) {
                                    displayText = wxString::Format("Error formatting display: %s", e.what());
                                }

                                // Send update event
                                wxCommandEvent event(EVT_METRIC_UPDATE);
                                event.SetString(displayText);
                                auto ui_start_time = std::chrono::high_resolution_clock::now();
                                wxQueueEvent(m_handler, event.Clone());
                                
                                // Wait for a short time to allow UI update
                                wxThread::Sleep(10);
                                
                                auto ui_end_time = std::chrono::high_resolution_clock::now();
                                auto ui_latency_us = std::chrono::duration_cast<std::chrono::microseconds>(ui_end_time - ui_start_time).count();
                                
                                // Update the display text with UI latency
                                try {
                                    displayText = wxString::Format(
                                        "Slippage: %.4f%%\n"
                                        "Impact Cost: %.4f%%\n"
                                        "Fees: %.4f%%\n"
                                        "Net Cost: %.4f%%\n"
                                        "Maker/Taker Ratio: %.2f\n"
                                        "Calculation Time: %.2f ms\n"
                                        "UI Update Time: %.2f ms",
                                        metrics[0] * 100.0,   // slippage in %
                                        metrics[1] * 100.0,   // impact in %
                                        metrics[2] * 100.0,   // fees in %
                                        metrics[3],          // net cost in %
                                        metrics[4],          // maker/taker proportion
                                        calc_latency_us / 1000.0,  // calculation time in ms
                                        ui_latency_us / 1000.0     // UI update time in ms
                                    );
                                    
                                    // Send final update with both latencies
                                    wxCommandEvent finalEvent(EVT_METRIC_UPDATE);
                                    finalEvent.SetString(displayText);
                                    wxQueueEvent(m_handler, finalEvent.Clone());
                                } catch (const std::exception& e) {
                                    wxString errorText = wxString::Format("Error updating UI latency: %s", e.what());
                                    wxCommandEvent errorEvent(EVT_METRIC_UPDATE);
                                    errorEvent.SetString(errorText);
                                    wxQueueEvent(m_handler, errorEvent.Clone());
                                }
                            }
                        }
                    }
                }
            });

            webSocket.start();
            while (webSocket.getReadyState() != ix::ReadyState::Closed && !TestDestroy()) {
                if (m_params->needs_reconnect) {
                    webSocket.stop();
                    break;
                }
                wxThread::Sleep(100);
            }
        }
        return nullptr;
    }
};

class MyFrame : public wxFrame {
public:
    MyFrame() : wxFrame(nullptr, wxID_ANY, "Trading Calculator", wxDefaultPosition, wxSize(700, 400)) {
        auto panel = new wxPanel(this);
        auto mainSizer = new wxBoxSizer(wxHORIZONTAL);

        // Input panel
        auto inputPanel = new wxPanel(panel);
        auto inputSizer = new wxBoxSizer(wxVERTICAL);
        
        exchangeCtrl = new wxTextCtrl(inputPanel, wxID_ANY, params.exchange);
        assetCtrl = new wxTextCtrl(inputPanel, wxID_ANY, params.asset);
        orderTypeCtrl = new wxTextCtrl(inputPanel, wxID_ANY, params.order_type);
        quantityCtrl = new wxTextCtrl(inputPanel, wxID_ANY, params.quantity);
        volatilityCtrl = new wxTextCtrl(inputPanel, wxID_ANY, params.volatility);
        feeTierCtrl = new wxTextCtrl(inputPanel, wxID_ANY, params.fee_tier);

        inputSizer->Add(new wxStaticText(inputPanel, wxID_ANY, "Exchange:"), 0, wxALL, 2);
        inputSizer->Add(exchangeCtrl, 0, wxEXPAND|wxALL, 2);
        inputSizer->Add(new wxStaticText(inputPanel, wxID_ANY, "Asset Pair:"), 0, wxALL, 2);
        inputSizer->Add(assetCtrl, 0, wxEXPAND|wxALL, 2);
        inputSizer->Add(new wxStaticText(inputPanel, wxID_ANY, "Order Type:"), 0, wxALL, 2);
        inputSizer->Add(orderTypeCtrl, 0, wxEXPAND|wxALL, 2);
        inputSizer->Add(new wxStaticText(inputPanel, wxID_ANY, "Quantity (USD):"), 0, wxALL, 2);
        inputSizer->Add(quantityCtrl, 0, wxEXPAND|wxALL, 2);
        inputSizer->Add(new wxStaticText(inputPanel, wxID_ANY, "Volatility (0-1):"), 0, wxALL, 2);
        inputSizer->Add(volatilityCtrl, 0, wxEXPAND|wxALL, 2);
        inputSizer->Add(new wxStaticText(inputPanel, wxID_ANY, "Fee Tier (basis points):"), 0, wxALL, 2);
        inputSizer->Add(feeTierCtrl, 0, wxEXPAND|wxALL, 2);

        inputPanel->SetSizer(inputSizer);

        // Display panel
        auto displayPanel = new wxPanel(panel);
        auto displaySizer = new wxBoxSizer(wxVERTICAL);
        
        resultDisplay = new wxStaticText(displayPanel, wxID_ANY, "Connecting to market data...", 
                                       wxDefaultPosition, wxDefaultSize, wxALIGN_CENTER);
        resultDisplay->SetFont(wxFont(14, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
        
        displaySizer->Add(resultDisplay, 1, wxEXPAND|wxALL, 10);
        displayPanel->SetSizer(displaySizer);

        mainSizer->Add(inputPanel, 1, wxEXPAND|wxALL, 5);
        mainSizer->Add(displayPanel, 1, wxEXPAND|wxALL, 5);
        panel->SetSizer(mainSizer);

        // Event bindings
        assetCtrl->Bind(wxEVT_TEXT, &MyFrame::OnAssetChanged, this);
        quantityCtrl->Bind(wxEVT_TEXT, &MyFrame::OnParamChanged, this);
        volatilityCtrl->Bind(wxEVT_TEXT, &MyFrame::OnParamChanged, this);
        feeTierCtrl->Bind(wxEVT_TEXT, &MyFrame::OnParamChanged, this);
        Bind(EVT_METRIC_UPDATE, &MyFrame::OnMetricUpdate, this);

        // Start WebSocket thread
        wsThread = new WebSocketThread(this, &params);
        wsThread->Run();
    }

    ~MyFrame() override {
        if (wsThread) wsThread->Delete();
    }

    void OnAssetChanged(wxCommandEvent& event) {
        std::lock_guard<std::mutex> lock(params.mutex);
        params.asset = assetCtrl->GetValue();
        params.needs_reconnect = true;
    }

    void OnParamChanged(wxCommandEvent& event) {
        std::lock_guard<std::mutex> lock(params.mutex);
        params.quantity = quantityCtrl->GetValue();
        params.volatility = volatilityCtrl->GetValue();
        params.fee_tier = feeTierCtrl->GetValue();
    }

    void OnMetricUpdate(wxCommandEvent& event) {
        resultDisplay->SetLabel(event.GetString());
        resultDisplay->Wrap(resultDisplay->GetSize().GetWidth());
    }

private:
    TradingParams params;
    wxTextCtrl *exchangeCtrl, *assetCtrl, *orderTypeCtrl, *quantityCtrl, *volatilityCtrl, *feeTierCtrl;
    wxStaticText* resultDisplay;
    WebSocketThread* wsThread = nullptr;
};

class MyApp : public wxApp {
public:
    bool OnInit() override {
        auto frame = new MyFrame();
        frame->Show();
        return true;
    }
};

wxIMPLEMENT_APP(MyApp);