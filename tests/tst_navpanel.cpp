// The production nav panel, loaded from a test-time ExplorerApp module and shown
// offscreen, over tabs whose trees are built as the format sessions build
// theirs: tab switches and filters keep each tab's navigation, categories
// included, and a superseded restore never reaches the view.

#include "core/documenttab.h"
#include "sessions/adaptersessionbase.h"

#include <QCoreApplication>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QRegularExpression>
#include <QTest>

#include <memory>

namespace {

// Messages -> EngineData -> EngineSpeed, EngineTemp; BrakeData -> BrakePressure;
// Nodes -> ECU0..ECU39. Messages and Nodes are categories. Titles carry the
// tab's prefix; keys do not, so two tabs share every key.
class TreeSession final : public AdapterSessionBase {
public:
    explicit TreeSession(const QString& prefix)
        : AdapterSessionBase(FormatId::Unknown, QStringLiteral("Fake"), prefix, prefix) {
        auto root = std::make_unique<TreeItem>();
        const auto entity = [this, &prefix](TreeItem* parent, const QString& title) {
            return appendNode(parent, prefix + title, {}, {}, SemanticKind::Entity, true);
        };
        TreeItem* messages = appendNode(root.get(), prefix + QStringLiteral("Messages"), {}, {},
                                        SemanticKind::Section);
        TreeItem* engine = entity(messages, QStringLiteral("EngineData"));
        entity(engine, QStringLiteral("EngineSpeed"));
        entity(engine, QStringLiteral("EngineTemp"));
        TreeItem* brake = entity(messages, QStringLiteral("BrakeData"));
        entity(brake, QStringLiteral("BrakePressure"));
        TreeItem* nodes = appendNode(root.get(), prefix + QStringLiteral("Nodes"), {}, {},
                                     SemanticKind::Section);
        for (int i = 0; i < 40; ++i) {
            entity(nodes, QStringLiteral("ECU%1").arg(i));
        }
        setRootItem(std::move(root));
    }

    void selectNode(quint64) override {}
};

} // namespace

class TestNavPanel : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void cleanup();
    void tabSwitchRestoresNavigation();
    void filterClearRestoresPreFilterNavigation();
    void noSelectionRestoredAfterFilter();
    void hiddenSelectionRestoredAfterFilter();
    void tabSwitchWhileFilteredKeepsSnapshot();
    void tabsDoNotShareNavigation();
    void expansionBeforeLayoutIsSaved();
    void rapidSwitchKeepsSavedNavigation();
    void rapidSwitchKeepsFilteredTab();
    void filterWhileRestorePendingKeepsSavedNavigation();
    void closedTabWithPendingRestore();
    void destroyedTargetOfPendingRestoreIsInert();

private:
    void show(DocumentTab* tab);
    QVariant call(const char* function);
    QVariant call(const char* function, const QVariant& argument);
    QString state() { return call("state").toString(); }
    bool settled() { return call("settled").toBool(); }
    void navigate(const QString& prefix);

    std::unique_ptr<DocumentTab> _tab_a;
    std::unique_ptr<DocumentTab> _tab_b;
    std::unique_ptr<QQmlApplicationEngine> _engine;
    QObject* _host = nullptr;
};

void TestNavPanel::initTestCase() {
    QQuickStyle::setStyle(QStringLiteral("Fusion"));
}

// Any QML warning or error fails the case.
void TestNavPanel::init() {
    QTest::failOnWarning(QRegularExpression(QStringLiteral("\\.qml:")));
    _tab_a = std::make_unique<DocumentTab>(std::make_unique<TreeSession>(QStringLiteral("A.")));
    _tab_b = std::make_unique<DocumentTab>(std::make_unique<TreeSession>(QStringLiteral("B.")));
    _engine = std::make_unique<QQmlApplicationEngine>();
    _engine->addImportPath(QStringLiteral(NAVPANEL_IMPORT_PATH));
    _engine->load(QUrl::fromLocalFile(QStringLiteral(NAVPANEL_HOST)));
    QVERIFY(!_engine->rootObjects().isEmpty());
    _host = _engine->rootObjects().first();
}

// The view goes before the tabs it showed.
void TestNavPanel::cleanup() {
    _engine.reset();
    _host = nullptr;
    _tab_a.reset();
    _tab_b.reset();
}

void TestNavPanel::show(DocumentTab* tab) {
    QObject* panel = _host->property("panel").value<QObject*>();
    panel->setProperty("tab", QVariant::fromValue<QObject*>(tab));
}

QVariant TestNavPanel::call(const char* function) {
    QVariant result;
    const bool invoked = QMetaObject::invokeMethod(_host, function, Q_RETURN_ARG(QVariant, result));
    return invoked ? result : QVariant(QStringLiteral("<no %1>").arg(QLatin1String(function)));
}

QVariant TestNavPanel::call(const char* function, const QVariant& argument) {
    QVariant result;
    const bool invoked = QMetaObject::invokeMethod(_host, function, Q_RETURN_ARG(QVariant, result),
                                                   Q_ARG(QVariant, argument));
    return invoked ? result : QVariant(QStringLiteral("<no %1>").arg(QLatin1String(function)));
}

// A category, an entity below it and a second category expanded, an entity
// current and the tree scrolled.
void TestNavPanel::navigate(const QString& prefix) {
    call("expand", prefix + QStringLiteral("Messages"));
    call("expand", prefix + QStringLiteral("EngineData"));
    call("expand", prefix + QStringLiteral("Nodes"));
    call("select", prefix + QStringLiteral("EngineSpeed"));
    call("scrollTo", 120);
}

// Switching away and back restores the expansion, categories and the entities
// below them included, the current row and the scroll position.
void TestNavPanel::tabSwitchRestoresNavigation() {
    show(_tab_a.get());
    QTRY_VERIFY(settled());
    navigate(QStringLiteral("A."));
    const QString before = state();
    QCOMPARE(before, QStringLiteral("expanded=[A.Messages, A.EngineData, A.Nodes] "
                                    "current=A.EngineSpeed contentY=120 rows=46"));

    show(_tab_b.get());
    QTRY_VERIFY(settled());
    show(_tab_a.get());
    QTRY_COMPARE(state(), before);
}

void TestNavPanel::filterClearRestoresPreFilterNavigation() {
    show(_tab_a.get());
    QTRY_VERIFY(settled());
    navigate(QStringLiteral("A."));
    const QString before = state();

    call("filter", QStringLiteral("Speed"));
    QTRY_VERIFY(state().endsWith(QStringLiteral("rows=3")));
    call("filter", QString());
    QTRY_COMPARE(state(), before);
}

// Clearing a filter restores an empty selection: the row selected while filtered
// is not kept.
void TestNavPanel::noSelectionRestoredAfterFilter() {
    show(_tab_a.get());
    QTRY_VERIFY(settled());
    call("expand", QStringLiteral("A.Messages"));
    const QString before = state();
    QCOMPARE(before, QStringLiteral("expanded=[A.Messages] current=none contentY=0 rows=4"));

    call("filter", QStringLiteral("Engine"));
    call("select", QStringLiteral("A.EngineData"));
    call("filter", QString());
    QTRY_COMPARE(state(), before);
}

// A current row hidden under a collapsed category is current again once the
// filter clears, and the category stays collapsed.
void TestNavPanel::hiddenSelectionRestoredAfterFilter() {
    show(_tab_a.get());
    QTRY_VERIFY(settled());
    call("expand", QStringLiteral("A.Messages"));
    call("select", QStringLiteral("A.BrakeData"));
    call("collapseAll");
    const QString before = state();
    QCOMPARE(before, QStringLiteral("expanded=[] current=A.BrakeData contentY=0 rows=2"));

    call("filter", QStringLiteral("Engine"));
    call("select", QStringLiteral("A.EngineData"));
    call("filter", QString());
    QTRY_COMPARE(state(), before);
}

// A switch while filtered saves the filtered view for the tab; clearing the
// filter afterwards still restores the navigation from before the filter.
void TestNavPanel::tabSwitchWhileFilteredKeepsSnapshot() {
    show(_tab_a.get());
    QTRY_VERIFY(settled());
    call("expand", QStringLiteral("A.Messages"));
    call("select", QStringLiteral("A.BrakeData"));
    const QString before = state();
    call("filter", QStringLiteral("Engine"));
    const QString filtered = state();

    show(_tab_b.get());
    QTRY_VERIFY(settled());
    show(_tab_a.get());
    QTRY_COMPARE(state(), filtered);
    QCOMPARE(call("searchText").toString(), QStringLiteral("Engine"));
    call("filter", QString());
    QTRY_COMPARE(state(), before);
}

// Both tabs use the same keys: a row expanded in one stays collapsed in the other.
void TestNavPanel::tabsDoNotShareNavigation() {
    show(_tab_a.get());
    QTRY_VERIFY(settled());
    call("expand", QStringLiteral("A.Messages"));

    show(_tab_b.get());
    QTRY_VERIFY(settled());
    QCOMPARE(state(), QStringLiteral("expanded=[] current=none contentY=0 rows=2"));
    show(_tab_a.get());
    QTRY_VERIFY(state().startsWith(QStringLiteral("expanded=[A.Messages] ")));
}

// Rows expanded in the switch's own turn, before the view laid them out, are
// saved: the row count saving walks follows the layout.
void TestNavPanel::expansionBeforeLayoutIsSaved() {
    show(_tab_a.get());
    QTRY_VERIFY(settled());
    call("expandUnlaid", QStringLiteral("A.Nodes"));
    call("expandUnlaid", QStringLiteral("A.Messages"));
    show(_tab_b.get());
    QTRY_VERIFY(settled());

    show(_tab_a.get());
    QTRY_VERIFY(settled());
    QVERIFY2(state().startsWith(QStringLiteral("expanded=[A.Messages, A.Nodes] ")), qPrintable(state()));
}

// A -> B -> A before any restore runs: only the last restore applies, and B's
// saved navigation is not replaced by its never-restored view. B expands a
// top-level row that A leaves collapsed, where B's superseded restore would show.
void TestNavPanel::rapidSwitchKeepsSavedNavigation() {
    show(_tab_b.get());
    QTRY_VERIFY(settled());
    call("expand", QStringLiteral("B.Messages"));
    call("expand", QStringLiteral("B.Nodes"));
    call("select", QStringLiteral("B.ECU3"));
    call("scrollTo", 60);
    const QString bState = state();
    show(_tab_a.get());
    QTRY_VERIFY(settled());
    call("expand", QStringLiteral("A.Nodes"));
    call("select", QStringLiteral("A.ECU5"));
    call("scrollTo", 120);
    const QString aState = state();
    QCOMPARE(aState, QStringLiteral("expanded=[A.Nodes] current=A.ECU5 contentY=120 rows=42"));

    show(_tab_b.get());
    show(_tab_a.get());
    QTRY_VERIFY(settled());
    QCOMPARE(state(), aState);

    show(_tab_b.get());
    QTRY_COMPARE(state(), bState);
}

// The same with B filtered: its filter text comes back without starting a new
// filter, and clearing it restores B's navigation from before the filter.
void TestNavPanel::rapidSwitchKeepsFilteredTab() {
    show(_tab_b.get());
    QTRY_VERIFY(settled());
    call("expand", QStringLiteral("B.Nodes"));
    call("select", QStringLiteral("B.ECU3"));
    const QString bBefore = state();
    call("filter", QStringLiteral("ECU1"));
    const QString bFiltered = state();
    show(_tab_a.get());
    QTRY_VERIFY(settled());
    navigate(QStringLiteral("A."));
    const QString aState = state();

    show(_tab_b.get());
    show(_tab_a.get());
    QTRY_VERIFY(settled());
    QCOMPARE(state(), aState);

    show(_tab_b.get());
    QTRY_COMPARE(state(), bFiltered);
    QCOMPARE(call("searchText").toString(), QStringLiteral("ECU1"));
    call("filter", QString());
    QTRY_COMPARE(state(), bBefore);
}

// A filter entered while the tab's restore is pending starts from the tab's
// saved navigation, not from the rebound view that has none. The restore it
// supersedes leaves the filtered view as the filter left it.
void TestNavPanel::filterWhileRestorePendingKeepsSavedNavigation() {
    show(_tab_a.get());
    QTRY_VERIFY(settled());
    navigate(QStringLiteral("A."));
    const QString aState = state();
    show(_tab_b.get());
    QTRY_VERIFY(settled());

    show(_tab_a.get());
    call("filter", QStringLiteral("Speed"));
    const QString filtered = state();
    QVERIFY(filtered.endsWith(QStringLiteral("rows=3")));
    QCoreApplication::processEvents();   // the superseded restore's turn
    QCOMPARE(state(), filtered);
    call("filter", QString());
    QTRY_COMPARE(state(), aState);
}

// Closing a tab whose restore is pending, in the controller's order: the next
// tab is shown first, then the closed tab is destroyed. The pending restore does
// nothing and no QML warning or error is raised.
void TestNavPanel::closedTabWithPendingRestore() {
    show(_tab_a.get());
    QTRY_VERIFY(settled());
    navigate(QStringLiteral("A."));
    const QString aState = state();
    show(_tab_b.get());

    show(_tab_a.get());
    _tab_b.reset();

    QTRY_VERIFY(settled());
    QCOMPARE(state(), aState);
}

// A restore whose target is destroyed while still shown does nothing: a
// destroyed tab reads no tree. The panel then shows the next tab normally.
void TestNavPanel::destroyedTargetOfPendingRestoreIsInert() {
    show(_tab_a.get());
    QTRY_VERIFY(settled());
    navigate(QStringLiteral("A."));
    const QString aState = state();
    show(_tab_b.get());
    _tab_b.reset();
    QCoreApplication::processEvents();

    show(_tab_a.get());
    QTRY_VERIFY(settled());
    QCOMPARE(state(), aState);
}

QTEST_MAIN(TestNavPanel)
#include "tst_navpanel.moc"
