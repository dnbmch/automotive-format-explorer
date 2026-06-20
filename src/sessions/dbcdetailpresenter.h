#pragma once

#include "core/detailpresenter.h"

#pragma push_macro("signals")
#undef signals
#include "dbc/dbc.pb.h"
#pragma pop_macro("signals")

class DbcDetailPresenter final : public DetailPresenter {
public:
    explicit DbcDetailPresenter(const dbc::DbcFile& document)
        : _document(document) {
    }

    QList<DetailSection> buildDetails(const NodeBinding& binding) const override;
    QString buildRawJson(const NodeBinding& binding) const override;

private:
    QList<DetailSection> nodeDetails(const DbcPath& path) const;
    QList<DetailSection> messageDetails(const DbcPath& path) const;
    QList<DetailSection> signalDetails(const DbcPath& path) const;
    QList<DetailSection> valueTableDetails(const DbcPath& path) const;
    QList<DetailSection> attributeDefinitionDetails(const DbcPath& path) const;
    QList<DetailSection> attributeDefaultDetails(const DbcPath& path) const;
    QList<DetailSection> attributeValueDetails(const DbcPath& path) const;
    QList<DetailSection> environmentVariableDetails(const DbcPath& path) const;
    QList<DetailSection> signalGroupDetails(const DbcPath& path) const;
    void appendNodeCrossReferences(QList<DetailSection>& sections, const std::string& nodeName) const;

    const dbc::DbcFile& _document;
};
