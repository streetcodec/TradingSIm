#include <wx/wx.h>
#include <wx/thread.h>
#include <atomic>
#include <mutex>
#include <string>
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
    wxString quantity = "100.0";
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
                                double quantity, volatility, fee_tier;
                                {
                                    std::lock_guard<std::mutex> lock(m_params->mutex);
                                    quantity = std::stod(m_params->quantity.ToStdString());
                                    volatility = std::stod(m_params->volatility.ToStdString());
                                    fee_tier = std::stod(m_params->fee_tier.ToStdString()) * 0.001;
                                }
                                
                                // Calculate slippage
                                auto metrics = calculateMetrics(
                                            quantity,
                                            volatility,
                                            fee_tier,
                                            asks,
                                            bids
                                        );

                                        // Format display text
                                wxString displayText = wxString::Format(
                                            "Slippage: %.4f%%\n"
                                            "Impact Cost: %.4f\n"
                                            "Temp Impact: %.4f\n"
                                            "Perm Impact: %.4f\n"
                                            "Total Impact: %.4f\n"
                                            "Impact (bps): %.2f",
                                            metrics[0] * 100, // slippage in %
                                            metrics[1],
                                            metrics[2],       // temp impact
                                            metrics[3],       // perm impact
                                            metrics[4]       // total impact
                                                   // impact in bps
                                        );
                                
                                // Send update event
                                wxCommandEvent event(EVT_METRIC_UPDATE);
                                event.SetString(displayText);
                                wxQueueEvent(m_handler, event.Clone());
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
