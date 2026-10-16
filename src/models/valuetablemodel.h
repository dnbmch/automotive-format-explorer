#pragma once

#include "models/tabledata.h"

#include <QAbstractTableModel>
#include <QString>
#include <QVariantList>

#include <cstdint>
#include <optional>
#include <vector>

// One column of a page after its time column, as the producer names it.
struct TableColumnHeader {
    QString name;
    bool numeric = false;  // a Values column
};

// What a producer says about the channel a table shows, whatever its values.
struct TableHeader {
    QString name;
    QString role;  // the tree's role text: "Text channel", "Byte channel", "Structure of N members", or an array's type and shape
    QString unit;
    std::uint64_t sampleCount = 0;  // as the source states it
    // The notes' word for the rows: "Texts", "Frames", "Bytes"; an array's
    // "Values".
    QString values;
    // The time master's name and unit, empty without one: a page's time
    // column, an array sample's time.
    QString timeName;
    QString timeUnit;
    // Page: its columns after the time column, in the page's order.
    std::vector<TableColumnHeader> columns;
    // Array: the dimension sizes, outer first, and the names of the row and
    // column axes.
    std::vector<std::uint64_t> dimensions;
    QString rowAxis;
    QString columnAxis;
};

// Why the table shows no rows.
enum class TableNote : std::uint8_t {
    None,
    Empty,    // nothing to show; the text says why
    Failed,   // the values could not be read
    Refused,  // a limit refused them
};

// Format-neutral model of the value table: a channel's samples in pages of
// kPageSamples (rows = the page's samples, columns the time against a time
// master, then the header's columns), or one sample of an array channel (rows ×
// columns = its slice, the axis labels as headers). A producer installs the
// channel, then the page or slice the view asks for through requestWanted.
// Every cell and header is a DisplayRole string.
class ValueTableModel : public QAbstractTableModel {
    Q_OBJECT

    Q_PROPERTY(Kind kind READ kind NOTIFY contentChanged)
    Q_PROPERTY(QString name READ name NOTIFY contentChanged)
    Q_PROPERTY(QString role READ role NOTIFY contentChanged)
    Q_PROPERTY(QString unit READ unit NOTIFY contentChanged)
    Q_PROPERTY(qulonglong sampleCount READ sampleCount NOTIFY contentChanged)
    Q_PROPERTY(QString countText READ countText NOTIFY contentChanged)
    Q_PROPERTY(State tableState READ tableState NOTIFY contentChanged)
    Q_PROPERTY(QString message READ message NOTIFY contentChanged)
    Q_PROPERTY(QString cornerText READ cornerText NOTIFY contentChanged)
    Q_PROPERTY(bool hasPrevious READ hasPrevious NOTIFY contentChanged)
    Q_PROPERTY(bool hasNext READ hasNext NOTIFY contentChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(double progress READ progress NOTIFY busyChanged)
    // Page.
    Q_PROPERTY(bool hasTimeColumn READ hasTimeColumn NOTIFY contentChanged)
    Q_PROPERTY(qulonglong firstRow READ firstRow NOTIFY contentChanged)
    Q_PROPERTY(QString rangeText READ rangeText NOTIFY contentChanged)
    Q_PROPERTY(qulonglong invalidCount READ invalidCount NOTIFY contentChanged)
    // Array.
    Q_PROPERTY(qulonglong sample READ sample NOTIFY contentChanged)
    Q_PROPERTY(QString timeText READ timeText NOTIFY contentChanged)
    Q_PROPERTY(QVariantList dimensions READ dimensions NOTIFY contentChanged)
    Q_PROPERTY(QVariantList slice READ slice NOTIFY contentChanged)

public:
    enum Kind { Page, Array };
    Q_ENUM(Kind)

    enum State {
        NoChannel,  // nothing selected
        Pending,    // the page or sample is being read
        Shown,      // rows are shown
        Empty,      // nothing to show; message() says why
        Failed,     // the values could not be read
        Refused,    // a limit refused them
    };
    Q_ENUM(State)

    explicit ValueTableModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;

    Kind kind() const;
    QString name() const;
    QString role() const;
    QString unit() const;
    qulonglong sampleCount() const;
    QString countText() const;
    State tableState() const;
    QString message() const;
    // "Sample" for a page; "<row axis> \ <column axis>" for an array.
    QString cornerText() const;
    bool hasPrevious() const;
    bool hasNext() const;
    bool busy() const;
    double progress() const;

    // A page's first column is its time against a time master.
    bool hasTimeColumn() const;
    qulonglong firstRow() const;
    // "Rows a–b of N" while rows are shown.
    QString rangeText() const;
    qulonglong invalidCount() const;

    qulonglong sample() const;
    // The shown sample's time and its unit, against a time master.
    QString timeText() const;
    QVariantList dimensions() const;
    QVariantList slice() const;

    // Whether `column` holds numbers: a page's Values column, every array
    // column; not the time column.
    Q_INVOKABLE bool numericColumn(int column) const;

    // View requests. Each clamps, clears the rows of a new page, sample or
    // slice, and emits requestWanted; asking for what is shown or pending does
    // nothing, unless it failed or was refused.
    //
    // The page holding `sample`, or that sample of an array, clamped to
    // [0, sampleCount − 1].
    Q_INVOKABLE void showSample(double sample);
    // The next or previous page, or an array's next or previous sample.
    Q_INVOKABLE void nextPage();
    Q_INVOKABLE void previousPage();
    // The index of outer dimension `dimension` of an array's slice, clamped to
    // its size.
    Q_INVOKABLE void setSlice(int dimension, double index);

    // Producer contract. Each call settles the model, then notifies; an
    // observer of any notification may call again or destroy the model, and
    // the newest content is shown.
    //
    // No channel: the empty table.
    void clear();
    // A channel shown in pages and its first page: null while it is read.
    void setPageChannel(TableHeader header, TablePagePtr page = {});
    // An array channel and its sample 0 with every slice index 0: null while
    // it is read.
    void setArrayChannel(TableHeader header, ArraySlicePtr slice = {});
    // The wanted page or slice, or a note on why none shows.
    void setPage(TablePagePtr page, TableNote note = TableNote::None, QString text = {});
    void setArraySlice(ArraySlicePtr slice, TableNote note = TableNote::None, QString text = {});
    void setBusy(bool busy);
    // Of the work in progress, from 0 to 1; negative when unknown.
    void setProgress(double fraction);

    // What the view wants: a page's first sample, or an array sample with the
    // outer indices of its slice.
    std::uint64_t wantedSample() const;
    const std::vector<std::uint64_t>& wantedSlice() const;
    const TablePagePtr& page() const;
    const ArraySlicePtr& arraySlice() const;

    // A value or label: "" for NaN, "∞" and "−∞", else 7 significant digits.
    static QString valueText(double value);
    // An axis label: the index, the point, or the interval "[a, b[" ("]a, b]"
    // when left-open).
    static QString labelText(const TableAxis& axis, std::size_t index);

signals:
    void contentChanged();
    void busyChanged();
    // The view moved to another page, sample or slice; wantedSample() and
    // wantedSlice() name it. Installing a result never emits it.
    void requestWanted();

private:
    // Everything one reset installs.
    struct Content {
        bool hasChannel = false;
        Kind kind = Page;
        TableHeader header;
        std::uint64_t wanted = 0;          // page: its first sample; array: the sample
        std::vector<std::uint64_t> slice;  // array: the outer indices of the slice
        TablePagePtr page;
        ArraySlicePtr array;
        TableNote note = TableNote::None;
        QString text;
    };

    static State stateOf(const Content& content);
    // The content shown, or the one a reset under way is about to install.
    const Content& latest() const;
    // Resets the model to `next`. While the pre-reset observers run, the
    // content waits in the model: one that installs again replaces it, and the
    // reset under way installs the newest. Stops once an observer destroys the
    // model.
    void install(Content next);
    // Wants `wanted` with `slice`: clears the rows and emits requestWanted.
    void request(std::uint64_t wanted, std::vector<std::uint64_t> slice);
    void showPosition(std::uint64_t position);

    Content _content;
    std::optional<Content> _arriving;
    bool _busy = false;
    double _progress = -1.0;
};
