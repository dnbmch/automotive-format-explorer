#pragma once

#include "core/detailpresenter.h"

#pragma push_macro("signals")
#undef signals
#include "ldf/ldf.pb.h"
#pragma pop_macro("signals")

class LdfDetailPresenter final : public DetailPresenter {
public:
    explicit LdfDetailPresenter(const ldf::LdfFile& document)
        : _document(document) {
    }

    QList<DetailSection> buildDetails(const NodeBinding& binding) const override;
    QString buildRawJson(const NodeBinding& binding) const override;

private:
    const ldf::Signal* findSignal(const std::string& name) const;

    QList<DetailSection> overviewDetails() const;
    QList<DetailSection> nodeDetails(const ldf::Node& node, bool isMaster) const;
    QList<DetailSection> frameDetails(const LdfPath& path) const;
    QList<DetailSection> frameSignalDetails(const LdfPath& path) const;
    QList<DetailSection> signalDetails(const LdfPath& path) const;
    QList<DetailSection> encodingDetails(const LdfPath& path) const;
    QList<DetailSection> scheduleDetails(const LdfPath& path) const;
    QList<DetailSection> eventFrameDetails(const LdfPath& path) const;
    QList<DetailSection> diagnosticAddressDetails(const LdfPath& path) const;
    QList<DetailSection> signalGroupDetails(const LdfPath& path) const;
    QList<DetailSection> diagnosticSignalDetails(const LdfPath& path) const;
    QList<DetailSection> diagnosticFrameDetails(const LdfPath& path) const;
    void appendEncodingValues(QList<DetailSection>& sections, const ldf::SignalEncoding& enc) const;
    void appendEncodingCrossReferences(QList<DetailSection>& sections, const std::string& encodingName) const;
    void appendNodeCrossReferences(QList<DetailSection>& sections, const std::string& nodeName) const;

    const ldf::LdfFile& _document;
};
