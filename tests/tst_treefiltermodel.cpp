// Filter semantics for the nav-panel tree filter (Ctrl+F).
//
// The proxy is thin on purpose — recursion and child auto-accept are delegated
// to QSortFilterProxyModel. What is pinned here is the resulting behaviour the
// nav panel depends on: a match keeps its ancestors reachable and its
// descendants in view, matching covers title and subtitle, and node-key lookup
// still resolves through the proxy while a filter is active.

#include "models/treefiltermodel.h"
#include "sessions/adaptersessionbase.h"

#include <QTest>

namespace {

// A session tree shaped like the format sessions build theirs: a category row
// with entities below it, all appended through the session base.
class CategorySession final : public AdapterSessionBase {
public:
    CategorySession()
        : AdapterSessionBase(FormatId::Unknown, QStringLiteral("Fake"), QStringLiteral("doc"),
                             QStringLiteral("doc")) {
        auto root = std::make_unique<TreeItem>();
        TreeItem* messages = appendNode(root.get(), QStringLiteral("Messages"), {}, {},
                                        SemanticKind::Section);
        TreeItem* engine = appendNode(messages, QStringLiteral("Engine"), {}, {},
                                      SemanticKind::Entity);
        appendNode(engine, QStringLiteral("EngineSpeed"), {}, {}, SemanticKind::Entity);
        appendNode(messages, QStringLiteral("Chassis"), {}, {}, SemanticKind::Entity);
        setRootItem(std::move(root));
    }

    void selectNode(quint64) override {}
};

quint64 nextKey = 1;

std::unique_ptr<TreeItem> makeItem(const QString& title, const QString& subtitle) {
    auto item = std::make_unique<TreeItem>();
    item->title = title;
    item->subtitle = subtitle;
    item->semanticKind = SemanticKind::Entity;
    item->selectable = true;
    item->nodeKey = nextKey++;
    return item;
}

void appendChild(TreeItem* parent, std::unique_ptr<TreeItem> child) {
    child->parent = parent;
    parent->children.push_back(std::move(child));
}

// root
//   Engine  (msg)      -> EngineSpeed (rpm), CoolantTemp (degC)
//   Chassis (msg)      -> WheelSpeed  (kph)
std::unique_ptr<TreeItem> makeTree() {
    nextKey = 1;
    auto root = std::make_unique<TreeItem>();
    root->semanticKind = SemanticKind::Root;

    auto engine = makeItem(QStringLiteral("Engine"), QStringLiteral("msg"));
    appendChild(engine.get(), makeItem(QStringLiteral("EngineSpeed"), QStringLiteral("rpm")));
    appendChild(engine.get(), makeItem(QStringLiteral("CoolantTemp"), QStringLiteral("degC")));

    auto chassis = makeItem(QStringLiteral("Chassis"), QStringLiteral("msg"));
    appendChild(chassis.get(), makeItem(QStringLiteral("WheelSpeed"), QStringLiteral("kph")));

    appendChild(root.get(), std::move(engine));
    appendChild(root.get(), std::move(chassis));
    return root;
}

QStringList topLevelTitles(const TreeFilterModel& proxy) {
    QStringList titles;
    for (int i = 0; i < proxy.rowCount({}); ++i) {
        titles << proxy.index(i, 0, {}).data(TreeModel::TitleRole).toString();
    }
    return titles;
}

QStringList childTitles(const TreeFilterModel& proxy, int topRow) {
    const QModelIndex parent = proxy.index(topRow, 0, {});
    QStringList titles;
    for (int i = 0; i < proxy.rowCount(parent); ++i) {
        titles << proxy.index(i, 0, parent).data(TreeModel::TitleRole).toString();
    }
    return titles;
}

} // namespace

class TestTreeFilterModel : public QObject {
    Q_OBJECT

private:
    TreeModel _source;
    TreeFilterModel _proxy;

private slots:
    void init();
    void emptyFilterShowsEverything();
    void leafMatchKeepsAncestorAndPrunesSiblings();
    void parentMatchKeepsAllChildren();
    void subtitleMatches();
    void matchingIsCaseInsensitive();
    void noMatchYieldsEmptyTree();
    void clearingFilterRestoresTree();
    void nodeKeyResolvesThroughProxyWhileFiltered();
    void nodeKeyOfFilteredOutRowIsInvalid();
    void categoryKeyResolvesThroughProxy();
};

void TestTreeFilterModel::init() {
    _source.setRoot(makeTree());
    _proxy.setSourceModel(&_source);
    _proxy.setFilterText(QString());
}

void TestTreeFilterModel::emptyFilterShowsEverything() {
    QCOMPARE(topLevelTitles(_proxy), QStringList({QStringLiteral("Engine"), QStringLiteral("Chassis")}));
    QCOMPARE(childTitles(_proxy, 0).size(), 2);
}

// The nav panel filters to a leaf and still needs the branch above it, or the
// match would be unreachable.
void TestTreeFilterModel::leafMatchKeepsAncestorAndPrunesSiblings() {
    _proxy.setFilterText(QStringLiteral("Coolant"));

    QCOMPARE(topLevelTitles(_proxy), QStringList({QStringLiteral("Engine")}));
    QCOMPARE(childTitles(_proxy, 0), QStringList({QStringLiteral("CoolantTemp")}));
}

// Filtering to a message keeps its signals in view (autoAcceptChildRows).
void TestTreeFilterModel::parentMatchKeepsAllChildren() {
    _proxy.setFilterText(QStringLiteral("Engine"));

    QCOMPARE(topLevelTitles(_proxy), QStringList({QStringLiteral("Engine")}));
    QCOMPARE(childTitles(_proxy, 0),
             QStringList({QStringLiteral("EngineSpeed"), QStringLiteral("CoolantTemp")}));
}

void TestTreeFilterModel::subtitleMatches() {
    _proxy.setFilterText(QStringLiteral("kph"));

    QCOMPARE(topLevelTitles(_proxy), QStringList({QStringLiteral("Chassis")}));
    QCOMPARE(childTitles(_proxy, 0), QStringList({QStringLiteral("WheelSpeed")}));
}

void TestTreeFilterModel::matchingIsCaseInsensitive() {
    _proxy.setFilterText(QStringLiteral("wheelspeed"));

    QCOMPARE(topLevelTitles(_proxy), QStringList({QStringLiteral("Chassis")}));
}

void TestTreeFilterModel::noMatchYieldsEmptyTree() {
    _proxy.setFilterText(QStringLiteral("nonexistent"));

    QCOMPARE(_proxy.rowCount({}), 0);
}

void TestTreeFilterModel::clearingFilterRestoresTree() {
    _proxy.setFilterText(QStringLiteral("Coolant"));
    QCOMPARE(_proxy.rowCount({}), 1);

    _proxy.setFilterText(QString());
    QCOMPARE(topLevelTitles(_proxy), QStringList({QStringLiteral("Engine"), QStringLiteral("Chassis")}));
}

// Selection restore: the center panel emits a node key and the nav panel must
// still be able to scroll to it while a filter is active.
void TestTreeFilterModel::nodeKeyResolvesThroughProxyWhileFiltered() {
    const QModelIndex sourceIdx = _source.indexForNodeKey(3); // CoolantTemp
    QVERIFY(sourceIdx.isValid());
    QCOMPARE(sourceIdx.data(TreeModel::TitleRole).toString(), QStringLiteral("CoolantTemp"));

    _proxy.setFilterText(QStringLiteral("Coolant"));

    const QModelIndex proxyIdx = _proxy.indexForNodeKey(3);
    QVERIFY(proxyIdx.isValid());
    QCOMPARE(proxyIdx.data(TreeModel::TitleRole).toString(), QStringLiteral("CoolantTemp"));
    QCOMPARE(_proxy.mapToSource(proxyIdx), sourceIdx);
}

void TestTreeFilterModel::nodeKeyOfFilteredOutRowIsInvalid() {
    _proxy.setFilterText(QStringLiteral("Coolant"));

    // WheelSpeed lives under the pruned Chassis branch.
    QVERIFY(_source.indexForNodeKey(5).isValid());
    QVERIFY(!_proxy.indexForNodeKey(5).isValid());
}

// The nav panel re-expands a category by its key, unfiltered and while one of
// its descendants matches.
void TestTreeFilterModel::categoryKeyResolvesThroughProxy() {
    CategorySession session;
    TreeFilterModel proxy;
    proxy.setSourceModel(session.treeModel());
    const QModelIndex messages = session.treeModel()->index(0, 0);
    const quint64 key = session.treeModel()->data(messages, TreeModel::NodeKeyRole).toULongLong();
    QVERIFY(key != 0);

    QCOMPARE(proxy.mapToSource(proxy.indexForNodeKey(key)), messages);
    proxy.setFilterText(QStringLiteral("Speed"));
    const QModelIndex filtered = proxy.indexForNodeKey(key);
    QVERIFY(filtered.isValid());
    QCOMPARE(filtered.data(TreeModel::TitleRole).toString(), QStringLiteral("Messages"));
}

QTEST_MAIN(TestTreeFilterModel)
#include "tst_treefiltermodel.moc"
