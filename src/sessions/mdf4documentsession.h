#pragma once

#include "models/plotseries.h"
#include "sessions/adaptersessionbase.h"

#include "mdf4/mdf4.pb.h"

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <utility>

class SignalPlotModel;

class Mdf4DocumentSession final : public AdapterSessionBase {
public:
    using DecodeFunction = std::function<PlotSeries(
        const QString&, std::uint32_t, std::uint32_t, std::uint64_t, std::uint64_t)>;

    Mdf4DocumentSession(QString displayName,
                        QString sourcePath,
                        mdf4::File document,
                        QList<DiagnosticMessage> diagnostics = {},
                        DecodeFunction decode = {});
    ~Mdf4DocumentSession() override;

    QUrl centerPanelSource() const override;
    QAbstractListModel* centerPanelModel() override;
    void selectNode(quint64 key) override;
    void moveModelsToThread(QThread* thread) override;

private:
    using ChannelKey = std::pair<std::uint32_t, std::uint32_t>;

    void buildTree();
    void clearPlot();
    void selectChannel(const Mdf4Path& path);

    mdf4::File _document;
    std::unique_ptr<SignalPlotModel> _plot_model;
    DecodeFunction _decode;
    std::map<ChannelKey, PlotSeries> _decode_cache;
    std::uint64_t _selection_generation = 0;
};
