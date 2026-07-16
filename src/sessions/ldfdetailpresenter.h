#pragma once

#include "core/detailpresenter.h"
#include "sessions/presentertext.h"

#pragma push_macro("signals")
#undef signals
#include "ldf/ldf.pb.h"
#pragma pop_macro("signals")

// Format-specific helpers live in ldfdetail so they don't collide with other
// backends' same-named global helpers (e.g. A2L's differently-defined
// numberText) when all backends link statically into one executable.
namespace ldfdetail {

// LDF renders doubles at full precision; the parser reports frame IDs and NADs
// as small unsigned values, so the integer path casts straight to qlonglong.
inline QString numberText(double value) {
    return QString::number(value, 'g', 12);
}

template<typename T>
QString numberText(T value) {
    return QString::number(static_cast<qlonglong>(value));
}

// A schedule entry is a frame reference or a typed diagnostic command. Needs
// LDF proto types, so it lives here rather than in the cross-format header.
QString scheduleEntryLabel(const ldf::ScheduleEntry& entry);

} // namespace ldfdetail

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
