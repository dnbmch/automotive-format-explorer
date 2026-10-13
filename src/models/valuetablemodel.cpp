#include "models/valuetablemodel.h"

#include <QLocale>
#include <QPointer>
#include <QStringList>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace {

QString countOf(std::uint64_t count) {
    return QLocale().toString(static_cast<qulonglong>(count));
}

// "" for NaN, the infinities by their signs, else `digits` significant digits.
QString numberText(double value, int digits) {
    if (std::isnan(value)) {
        return {};
    }
    if (std::isinf(value)) {
        return value > 0.0 ? QStringLiteral("∞") : QStringLiteral("−∞");
    }
    return QString::number(value, 'g', digits);
}

// A time: 12 significant digits.
QString timeCell(double time) {
    return numberText(time, 12);
}

} // namespace

ValueTableModel::ValueTableModel(QObject* parent)
    : QAbstractTableModel(parent) {
}

int ValueTableModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) {
        return 0;
    }
    if (_content.kind == Text) {
        return _content.page ? static_cast<int>(_content.page->rows()) : 0;
    }
    return _content.array ? static_cast<int>(_content.array->rows) : 0;
}

int ValueTableModel::columnCount(const QModelIndex& parent) const {
    if (parent.isValid() || !_content.hasChannel) {
        return 0;
    }
    if (_content.kind == Text) {
        return _content.header.timeName.isEmpty() ? 1 : 2;
    }
    return _content.array ? static_cast<int>(_content.array->columns) : 0;
}

QVariant ValueTableModel::data(const QModelIndex& index, int role) const {
    if (role != Qt::DisplayRole || !checkIndex(index, CheckIndexOption::IndexIsValid)) {
        return {};
    }
    const auto row = static_cast<std::size_t>(index.row());
    const auto column = static_cast<std::size_t>(index.column());
    if (_content.kind == Text) {
        const TextPage& page = *_content.page;
        if (column == 0 && !_content.header.timeName.isEmpty()) {
            return timeCell(page.time[row]);
        }
        const std::string_view text = page.text(row);
        return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
    }
    const ArraySlice& slice = *_content.array;
    return valueText(slice.values[row * slice.columns + column]);
}

QVariant ValueTableModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (role != Qt::DisplayRole || section < 0) {
        return {};
    }
    const auto index = static_cast<std::size_t>(section);
    if (orientation == Qt::Vertical) {
        if (section >= rowCount()) {
            return {};
        }
        return _content.kind == Text
            ? QString::number(static_cast<qulonglong>(_content.page->firstSample + index))
            : labelText(_content.array->rowAxis, index);
    }
    if (section >= columnCount()) {
        return {};
    }
    if (_content.kind == Array) {
        return labelText(_content.array->columnAxis, index);
    }
    const TableHeader& header = _content.header;
    if (section == 0 && !header.timeName.isEmpty()) {
        return header.timeUnit.isEmpty()
            ? header.timeName
            : QStringLiteral("%1 [%2]").arg(header.timeName, header.timeUnit);
    }
    return QStringLiteral("Text");
}

ValueTableModel::Kind ValueTableModel::kind() const { return _content.kind; }
QString ValueTableModel::name() const { return _content.header.name; }
QString ValueTableModel::role() const { return _content.header.role; }
QString ValueTableModel::unit() const { return _content.header.unit; }
qulonglong ValueTableModel::sampleCount() const { return _content.header.sampleCount; }

QString ValueTableModel::countText() const {
    if (!_content.hasChannel) {
        return {};
    }
    const std::uint64_t count = _content.header.sampleCount;
    return QStringLiteral("%1 %2").arg(countOf(count), count == 1 ? QStringLiteral("sample")
                                                                  : QStringLiteral("samples"));
}

ValueTableModel::State ValueTableModel::tableState() const {
    return stateOf(_content);
}

QString ValueTableModel::message() const {
    const Content& content = _content;
    const bool text = content.kind == Text;
    switch (stateOf(content)) {
    case NoChannel:
        return {};
    case Pending:
        return text ? QStringLiteral("Reading texts…") : QStringLiteral("Reading values…");
    case Shown: {
        QStringList notes;
        if (!content.text.isEmpty()) {
            notes << content.text;
        }
        if (!text) {
            for (const TableAxis* axis : {&content.array->rowAxis, &content.array->columnAxis}) {
                if (!axis->note.empty()) {
                    notes << QString::fromStdString(axis->note);
                }
            }
        }
        return notes.join(QStringLiteral("  ·  "));
    }
    case Empty:
        if (!content.text.isEmpty()) {
            return content.text;
        }
        return content.header.sampleCount == 0
            ? QStringLiteral("No samples recorded")
            : QStringLiteral("No samples: the recording ends before sample %1")
                  .arg(countOf(content.wanted));
    case Failed:
        if (!content.text.isEmpty()) {
            return content.text;
        }
        return text ? QStringLiteral("Texts could not be read")
                    : QStringLiteral("Values could not be read");
    case Refused:
        if (!content.text.isEmpty()) {
            return content.text;
        }
        return text ? QStringLiteral("Texts exceed the viewer's limits")
                    : QStringLiteral("Values exceed the viewer's limits");
    }
    return {};
}

QString ValueTableModel::cornerText() const {
    if (!_content.hasChannel) {
        return {};
    }
    if (_content.kind == Text) {
        return QStringLiteral("Sample");
    }
    const TableHeader& header = _content.header;
    return header.columnAxis.isEmpty()
        ? header.rowAxis
        : QStringLiteral("%1 \\ %2").arg(header.rowAxis, header.columnAxis);
}

bool ValueTableModel::hasPrevious() const {
    return _content.hasChannel && _content.wanted > 0;
}

bool ValueTableModel::hasNext() const {
    const std::uint64_t step = _content.kind == Text ? kTextPageSamples : 1;
    const std::uint64_t count = _content.header.sampleCount;
    return _content.hasChannel && count > _content.wanted && count - _content.wanted > step;
}

bool ValueTableModel::busy() const { return _busy; }
double ValueTableModel::progress() const { return _progress; }

qulonglong ValueTableModel::firstRow() const { return _content.wanted; }

QString ValueTableModel::rangeText() const {
    if (_content.kind != Text || stateOf(_content) != Shown) {
        return {};
    }
    const TextPage& page = *_content.page;
    return QStringLiteral("Rows %1–%2 of %3")
        .arg(countOf(page.firstSample), countOf(page.firstSample + (page.rows() - 1)),
             countOf(_content.header.sampleCount));
}

qulonglong ValueTableModel::invalidCount() const {
    return _content.kind == Text && _content.page ? _content.page->invalidValues : 0;
}

qulonglong ValueTableModel::sample() const { return _content.wanted; }

// Against a time master only: without one the time is the sample's index.
QString ValueTableModel::timeText() const {
    const TableHeader& header = _content.header;
    if (_content.kind != Array || !_content.array || header.timeName.isEmpty()) {
        return {};
    }
    const QString time = timeCell(_content.array->time);
    return header.timeUnit.isEmpty() ? time : QStringLiteral("%1 %2").arg(time, header.timeUnit);
}

QVariantList ValueTableModel::dimensions() const {
    QVariantList sizes;
    for (const std::uint64_t size : _content.header.dimensions) {
        sizes.push_back(static_cast<qulonglong>(size));
    }
    return sizes;
}

QVariantList ValueTableModel::slice() const {
    QVariantList indices;
    for (const std::uint64_t index : _content.slice) {
        indices.push_back(static_cast<qulonglong>(index));
    }
    return indices;
}

// The user's number: below zero, or none, is the first sample; past the 64-bit
// range, the last.
void ValueTableModel::showSample(double sample) {
    constexpr double kBeyond = 18446744073709551616.0;  // 2^64
    showPosition(!(sample > 0.0) ? 0
                 : sample >= kBeyond ? std::numeric_limits<std::uint64_t>::max()
                                     : static_cast<std::uint64_t>(sample));
}

void ValueTableModel::nextPage() {
    const Content& now = latest();
    showPosition(now.wanted + (now.kind == Text ? kTextPageSamples : 1));
}

void ValueTableModel::previousPage() {
    const Content& now = latest();
    const std::uint64_t step = now.kind == Text ? kTextPageSamples : 1;
    showPosition(now.wanted > step ? now.wanted - step : 0);
}

void ValueTableModel::setSlice(int dimension, double index) {
    const Content& now = latest();
    if (!now.hasChannel || now.kind != Array || dimension < 0 ||
        static_cast<std::size_t>(dimension) >= now.slice.size()) {
        return;
    }
    const std::uint64_t size = now.header.dimensions[static_cast<std::size_t>(dimension)];
    const std::uint64_t last = size > 0 ? size - 1 : 0;
    std::vector<std::uint64_t> slice = now.slice;
    slice[static_cast<std::size_t>(dimension)] =
        !(index > 0.0) ? 0
        : index >= static_cast<double>(last) ? last
                                             : static_cast<std::uint64_t>(index);
    request(now.wanted, std::move(slice));
}

void ValueTableModel::showPosition(std::uint64_t position) {
    const Content& now = latest();
    if (!now.hasChannel) {
        return;
    }
    const std::uint64_t count = now.header.sampleCount;
    position = std::min(position, count > 0 ? count - 1 : 0);
    request(now.kind == Text ? textPageFirst(position) : position, now.slice);
}

void ValueTableModel::request(std::uint64_t wanted, std::vector<std::uint64_t> slice) {
    const Content& now = latest();
    const State state = stateOf(now);
    if (wanted == now.wanted && slice == now.slice && state != Failed && state != Refused) {
        return;
    }
    Content next = now;
    next.wanted = wanted;
    next.slice = std::move(slice);
    next.page.reset();
    next.array.reset();
    next.note = TableNote::None;
    next.text.clear();
    QPointer<ValueTableModel> self(this);
    install(std::move(next));
    if (self) {
        emit requestWanted();
    }
}

void ValueTableModel::clear() {
    install(Content{});
}

void ValueTableModel::setTextChannel(TableHeader header, TextPagePtr page) {
    Content next;
    next.hasChannel = true;
    next.kind = Text;
    next.header = std::move(header);
    next.page = std::move(page);
    install(std::move(next));
}

void ValueTableModel::setArrayChannel(TableHeader header, ArraySlicePtr slice) {
    Content next;
    next.hasChannel = true;
    next.kind = Array;
    next.slice.assign(header.dimensions.size() > 2 ? header.dimensions.size() - 2 : 0, 0);
    next.header = std::move(header);
    next.array = std::move(slice);
    install(std::move(next));
}

void ValueTableModel::setTextPage(TextPagePtr page, TableNote note, QString text) {
    Content next = latest();
    next.page = std::move(page);
    next.note = note;
    next.text = std::move(text);
    install(std::move(next));
}

void ValueTableModel::setArraySlice(ArraySlicePtr slice, TableNote note, QString text) {
    Content next = latest();
    next.array = std::move(slice);
    next.note = note;
    next.text = std::move(text);
    install(std::move(next));
}

void ValueTableModel::setBusy(bool busy) {
    if (_busy == busy) {
        return;
    }
    _busy = busy;
    emit busyChanged();
}

void ValueTableModel::setProgress(double fraction) {
    if (_progress == fraction) {
        return;
    }
    _progress = fraction;
    emit busyChanged();
}

std::uint64_t ValueTableModel::wantedSample() const { return latest().wanted; }
const std::vector<std::uint64_t>& ValueTableModel::wantedSlice() const { return latest().slice; }
const TextPagePtr& ValueTableModel::page() const { return _content.page; }
const ArraySlicePtr& ValueTableModel::arraySlice() const { return _content.array; }

QString ValueTableModel::valueText(double value) {
    return numberText(value, 7);
}

QString ValueTableModel::labelText(const TableAxis& axis, std::size_t index) {
    switch (axis.kind) {
    case TableAxis::Kind::Index:
        break;
    case TableAxis::Kind::Points:
        return valueText(axis.values[index]);
    case TableAxis::Kind::Intervals:
        return (axis.leftOpen ? QStringLiteral("]%1, %2]") : QStringLiteral("[%1, %2["))
            .arg(valueText(axis.values[index]), valueText(axis.values[index + 1]));
    }
    return QString::number(static_cast<qulonglong>(index));
}

ValueTableModel::State ValueTableModel::stateOf(const Content& content) {
    if (!content.hasChannel) {
        return NoChannel;
    }
    switch (content.note) {
    case TableNote::Empty: return Empty;
    case TableNote::Failed: return Failed;
    case TableNote::Refused: return Refused;
    case TableNote::None: break;
    }
    if (content.kind == Text && content.page) {
        return content.page->rows() > 0 ? Shown : Empty;
    }
    if (content.kind == Array && content.array) {
        return content.array->rows > 0 && content.array->columns > 0 ? Shown : Empty;
    }
    return Pending;
}

const ValueTableModel::Content& ValueTableModel::latest() const {
    return _arriving ? *_arriving : _content;
}

// The content changes inside the reset, so an observer of the reset already
// reads the new content.
void ValueTableModel::install(Content next) {
    const bool resetUnderWay = _arriving.has_value();
    _arriving = std::move(next);
    if (resetUnderWay) {
        return;
    }
    QPointer<ValueTableModel> self(this);
    beginResetModel();
    if (!self) {
        return;
    }
    _content = std::move(*_arriving);
    _arriving.reset();
    endResetModel();
    if (self) {
        emit contentChanged();
    }
}
