// A tab's tree navigation and its pre-filter snapshot: entering a filter keeps
// the navigation, and clearing the filter gives exactly that navigation back,
// whatever was saved while the filter was active.

#include "core/documenttab.h"
#include "sessions/adaptersessionbase.h"

#include <QSignalSpy>
#include <QTest>

namespace {

// Messages -> Engine -> EngineSpeed, and Nodes -> ECU, keyed 1..5 in that order.
class SmallSession final : public AdapterSessionBase {
public:
    SmallSession()
        : AdapterSessionBase(FormatId::Unknown, QStringLiteral("Fake"), QStringLiteral("doc"),
                             QStringLiteral("doc")) {
        auto root = std::make_unique<TreeItem>();
        TreeItem* messages = appendNode(root.get(), QStringLiteral("Messages"), {}, {},
                                        SemanticKind::Section);
        TreeItem* engine = appendNode(messages, QStringLiteral("Engine"), {}, {},
                                      SemanticKind::Entity, true);
        appendNode(engine, QStringLiteral("EngineSpeed"), {}, {}, SemanticKind::Entity, true);
        TreeItem* nodes = appendNode(root.get(), QStringLiteral("Nodes"), {}, {},
                                     SemanticKind::Section);
        appendNode(nodes, QStringLiteral("ECU"), {}, {}, SemanticKind::Entity, true);
        setRootItem(std::move(root));
    }

    void selectNode(quint64) override {}
};

QVariantList keys(std::initializer_list<qulonglong> values) {
    QVariantList list;
    for (const qulonglong value : values) {
        list << QVariant::fromValue(value);
    }
    return list;
}

// The tab's navigation as one comparable value.
QVariantList navigation(const DocumentTab& tab) {
    return {tab.expandedKeys(), tab.currentKey(), tab.contentY()};
}

} // namespace

class TestDocumentTab : public QObject {
    Q_OBJECT

private slots:
    void newTabHasNoNavigation();
    void snapshotRestoredWhenFilterClears();
    void snapshotSurvivesSaveWhileFiltered();
    void refilteringKeepsSnapshot();
    void filterReachesProxy();
};

void TestDocumentTab::newTabHasNoNavigation() {
    DocumentTab tab(std::make_unique<SmallSession>());
    QVERIFY(tab.expandedKeys().isEmpty());
    QCOMPARE(tab.currentKey(), qulonglong(0));
    QCOMPARE(tab.contentY(), 0.0);
    QVERIFY(tab.filterText().isEmpty());
    QCOMPARE(tab.treeModel()->rowCount(), 2);
}

void TestDocumentTab::snapshotRestoredWhenFilterClears() {
    DocumentTab tab(std::make_unique<SmallSession>());
    tab.saveNavigation(keys({1, 2}), 3, 40);
    const QVariantList before = navigation(tab);
    QSignalSpy changed(&tab, &DocumentTab::navigationChanged);

    tab.setFilterText(QStringLiteral("Speed"));
    QCOMPARE(changed.count(), 0);
    tab.setFilterText(QString());

    QCOMPARE(navigation(tab), before);
    QCOMPARE(changed.count(), 1);
}

// A tab switch while filtered saves the filtered view; clearing the filter still
// gives back the navigation from before the filter.
void TestDocumentTab::snapshotSurvivesSaveWhileFiltered() {
    DocumentTab tab(std::make_unique<SmallSession>());
    tab.saveNavigation(keys({1}), 2, 0);
    const QVariantList before = navigation(tab);

    tab.setFilterText(QStringLiteral("Speed"));
    tab.saveNavigation(keys({1, 2, 4}), 3, 12);
    QCOMPARE(navigation(tab), QVariantList({keys({1, 2, 4}), qulonglong(3), 12.0}));
    tab.setFilterText(QString());

    QCOMPARE(navigation(tab), before);
}

// Changing a non-empty filter keeps the snapshot taken when the filter started.
void TestDocumentTab::refilteringKeepsSnapshot() {
    DocumentTab tab(std::make_unique<SmallSession>());
    tab.saveNavigation(keys({4}), 5, 8);
    const QVariantList before = navigation(tab);

    tab.setFilterText(QStringLiteral("Sp"));
    tab.saveNavigation(keys({1, 2}), 3, 0);
    tab.setFilterText(QStringLiteral("Speed"));
    tab.saveNavigation(keys({1}), 0, 0);
    tab.setFilterText(QString());

    QCOMPARE(navigation(tab), before);
}

void TestDocumentTab::filterReachesProxy() {
    DocumentTab tab(std::make_unique<SmallSession>());
    QSignalSpy filter(&tab, &DocumentTab::filterTextChanged);

    tab.setFilterText(QStringLiteral("ECU"));
    tab.setFilterText(QStringLiteral("ECU"));

    QCOMPARE(filter.count(), 1);
    QCOMPARE(tab.filterText(), QStringLiteral("ECU"));
    QCOMPARE(tab.treeModel()->filterText(), QStringLiteral("ECU"));
    QCOMPARE(tab.treeModel()->rowCount(), 1);
    const QModelIndex nodes = tab.treeModel()->index(0, 0);
    QCOMPARE(nodes.data(TreeModel::TitleRole).toString(), QStringLiteral("Nodes"));
}

QTEST_GUILESS_MAIN(TestDocumentTab)
#include "tst_documenttab.moc"
