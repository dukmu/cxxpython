#include "internal/codeobject.hpp"
#include "internal/abstract.hpp"

#include <algorithm>
#include <unordered_map>

namespace cxxpython::core::detail {
namespace {
void type_error(Attachment &c, std::u32string message, ReturnSlot &out)
{
    raise_error(c, CoreObject::TypeErrorType, std::move(message), out);
}
bool raised(ReturnSlot &out)
{
    return out.tag() == ResultTag::Raised;
}
constexpr std::string_view bytecode_magic = "CXXB\1", line_magic = "CXXL\1",
                           region_magic = "CXXE\1";
[[noreturn]] void invalid_image(const char *message)
{
    throw CodeValidationError(CodeErrorKind::Operand, message);
}
void word(std::string &out, std::size_t value)
{
    if (value > std::numeric_limits<std::uint32_t>::max())
        invalid_image("native code image exceeds its 32-bit size limit");
    for (unsigned shift = 0; shift != 32; shift += 8)
        out.push_back(static_cast<char>((value >> shift) & 255));
}
class ImageReader {
  public:
    ImageReader(Attachment &context, std::string_view bytes, std::string_view magic)
        : context_(context), bytes_(bytes)
    {
        if (!bytes_.starts_with(magic))
            invalid_image("code image is not supported cxxpython bytecode");
        offset_ = magic.size();
    }
    std::uint8_t byte()
    {
        if (offset_ == bytes_.size())
            invalid_image("truncated native code image");
        const auto result = static_cast<std::uint8_t>(bytes_[offset_++]);
        if ((offset_ & 4095) == 0)
            context_.poll();
        return result;
    }
    std::uint32_t word()
    {
        std::uint32_t result = 0;
        for (unsigned shift = 0; shift != 32; shift += 8)
            result |= static_cast<std::uint32_t>(byte()) << shift;
        return result;
    }
    std::uint32_t count(std::size_t minimum_bytes)
    {
        const auto result = word();
        if (result > (bytes_.size() - offset_) / minimum_bytes)
            invalid_image("truncated native code table");
        return result;
    }
    std::u32string text()
    {
        const auto size = count(4);
        std::u32string result;
        result.reserve(size);
        for (std::size_t i = 0; i < size; ++i) {
            const auto point = word();
            if (point > 0x10ffff)
                invalid_image("invalid code metadata character");
            result.push_back(static_cast<char32_t>(point));
        }
        return result;
    }
    void finish() const
    {
        if (offset_ != bytes_.size())
            invalid_image("trailing data in native code image");
    }

  private:
    Attachment &context_;
    std::string_view bytes_;
    std::size_t offset_ = 0;
};
bool string_tuple(Attachment &c, Value value, std::vector<std::u32string> &result, ReturnSlot &out)
{
    if (!c.has_layout<TupleObject>(value) ||
        !is_subtype(c, c.type_of(value), c.builtin(CoreObject::TupleType))) {
        type_error(c, U"code names must be tuples of strings", out);
        return false;
    }
    for (auto item : c.payload<TupleObject>(value).items) {
        if (!c.has_layout<StrObject>(item) ||
            !is_subtype(c, c.type_of(item), c.builtin(CoreObject::StrType))) {
            type_error(c, U"code names must be tuples of strings", out);
            return false;
        }
        result.push_back(c.payload<StrObject>(item).value);
    }
    return true;
}
void code_name(Attachment &c, Value value, ReturnSlot &out)
{
    out.ok(c.payload<CodeObject>(value).name);
}
void code_qualname(Attachment &c, Value value, ReturnSlot &out)
{
    out.ok(c.payload<CodeObject>(value).qualname);
}
void code_filename(Attachment &c, Value value, ReturnSlot &out)
{
    out.ok(c.payload<CodeObject>(value).filename);
}
void code_constants(Attachment &c, Value value, ReturnSlot &out)
{
    out.ok(c.payload<CodeObject>(value).constants);
}
void code_varnames(Attachment &c, Value value, ReturnSlot &out)
{
    out.ok(c.payload<CodeObject>(value).varnames);
}
void code_names(Attachment &c, Value value, ReturnSlot &out)
{
    out.ok(c.payload<CodeObject>(value).names);
}
void code_cellvars(Attachment &c, Value value, ReturnSlot &out)
{
    out.ok(c.payload<CodeObject>(value).cellvars);
}
void code_freevars(Attachment &c, Value value, ReturnSlot &out)
{
    out.ok(c.payload<CodeObject>(value).freevars);
}
void code_stacksize(Attachment &c, Value value, ReturnSlot &out)
{
    make_int(c, static_cast<std::int64_t>(c.payload<CodeObject>(value).program.max_stack()), out);
}
void code_firstline(Attachment &c, Value value, ReturnSlot &out)
{
    make_int(c, c.payload<CodeObject>(value).program.first_line(), out);
}
void code_flags(Attachment &c, Value value, ReturnSlot &out)
{
    make_int(c, c.payload<CodeObject>(value).program.flags(), out);
}
void parameter_count(Attachment &c, Value value, std::optional<ParameterKind> kind, ReturnSlot &out)
{
    const auto &parameters = c.payload<CodeObject>(value).program.signature().parameters;
    const auto count =
        std::count_if(parameters.begin(), parameters.end(), [&](const auto &parameter) {
            return kind ? parameter.kind == *kind : parameter.kind != ParameterKind::KeywordOnly;
        });
    make_int(c, count, out);
}
void code_argcount(Attachment &c, Value value, ReturnSlot &out)
{
    parameter_count(c, value, {}, out);
}
void code_posonlycount(Attachment &c, Value value, ReturnSlot &out)
{
    parameter_count(c, value, ParameterKind::PositionalOnly, out);
}
void code_kwonlycount(Attachment &c, Value value, ReturnSlot &out)
{
    parameter_count(c, value, ParameterKind::KeywordOnly, out);
}
void code_nlocals(Attachment &c, Value value, ReturnSlot &out)
{
    make_int(c,
             static_cast<std::int64_t>(
                 c.payload<TupleObject>(c.payload<CodeObject>(value).varnames).items.size()),
             out);
}
template <bool Lines>
void code_locations(Attachment &c, Value, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(c);
    auto self = roots.root(args[0]);
    if (c.type_of(self) != c.builtin(CoreObject::CodeType) ||
        !c.has_layout<CodeObject>(self.get(c))) {
        type_error(c, U"code position methods require a code object", out);
        return;
    }
    const auto positions = c.payload<CodeObject>(self).program.positions();
    std::vector<Value> rows;
    for (std::size_t start = 0; start < positions.size();) {
        RootScope row_roots(c);
        ReturnSlot value(c);
        std::vector<Value> fields;
        auto number = [&](std::optional<std::int64_t> n) {
            if (n)
                make_int(c, *n, value);
            else
                value.ok(c.builtin(CoreObject::None));
            if (raised(value)) {
                out.raised(value.exception());
                return false;
            }
            fields.push_back(row_roots.root(value.value()).get(c));
            return true;
        };
        const auto position = positions[start];
        auto end = start + 1;
        if constexpr (Lines) {
            while (end < positions.size() && positions[end].line == position.line) {
                if ((end & 4095) == 0)
                    c.poll();
                ++end;
            }
            if (!number(static_cast<std::int64_t>(start)) ||
                !number(static_cast<std::int64_t>(end)) ||
                !number(position.line ? std::optional<std::int64_t>{position.line} : std::nullopt))
                return;
        } else {
            for (const auto n :
                 {position.line, position.end_line, position.column, position.end_column})
                if (!number(position.line && n != std::numeric_limits<std::uint32_t>::max()
                                ? std::optional<std::int64_t>{n}
                                : std::nullopt))
                    return;
        }
        cxxpython::core::make_tuple(c, fields, value);
        if (raised(value)) {
            out.raised(value.exception());
            return;
        }
        rows.push_back(roots.root(value.value()).get(c));
        start = end;
        if ((start & 4095) == 0)
            c.poll();
    }
    // The existing tuple iterator owns the immutable snapshot, not borrowed Code data.
    ReturnSlot snapshot(c);
    cxxpython::core::make_tuple(c, rows, snapshot);
    if (raised(snapshot)) {
        out.raised(snapshot.exception());
        return;
    }
    get_iter(c, roots.root(snapshot.value()), out);
}
void code_bytecode(Attachment &c, Value value, ReturnSlot &out)
{
    out.ok(c.payload<CodeObject>(value).bytecode);
}
void code_linetable(Attachment &c, Value value, ReturnSlot &out)
{
    out.ok(c.payload<CodeObject>(value).linetable);
}
void code_exceptiontable(Attachment &c, Value value, ReturnSlot &out)
{
    out.ok(c.payload<CodeObject>(value).exceptiontable);
}
void code_lnotab(Attachment &c, Value value, ReturnSlot &out)
{
    const auto &program = c.payload<CodeObject>(value).program;
    std::size_t previous_pc = 0;
    auto previous_line = static_cast<std::int64_t>(program.first_line());
    std::string result;
    for (std::size_t pc = 0; pc < program.positions().size(); ++pc) {
        const auto line = program.positions()[pc].line;
        if (!line || line == previous_line)
            continue;
        auto address = pc - previous_pc;
        auto delta = static_cast<std::int64_t>(line) - previous_line;
        while (address > 255) {
            result.append({static_cast<char>(255), 0});
            address -= 255;
        }
        while (delta > 127 || delta < -128) {
            const auto step = delta > 0 ? 127 : -128;
            result.append({static_cast<char>(address), static_cast<char>(step)});
            address = 0;
            delta -= step;
        }
        result.append({static_cast<char>(address), static_cast<char>(delta)});
        previous_pc = pc;
        previous_line = line;
    }
    make_bytes(c, std::move(result), out);
}
void code_new(Attachment &c, Value, std::span<const Value> args, ReturnSlot &out)
{
    if (args[0] != c.builtin(CoreObject::CodeType)) {
        type_error(c, U"code.__new__ requires code", out);
        return;
    }
    std::array<std::uint32_t, 19> numbers{};
    for (auto i : {1U, 2U, 3U, 4U, 5U, 6U, 14U}) {
        std::int64_t number;
        if (!index_as_int64(c, args[i], number, out))
            return;
        if (number < 0 ||
            static_cast<std::uint64_t>(number) > std::numeric_limits<std::uint32_t>::max()) {
            raise_error(c, CoreObject::ValueErrorType, U"code integer argument out of range", out);
            return;
        }
        numbers[i] = static_cast<std::uint32_t>(number);
    }
    for (auto i : {7U, 15U, 16U})
        if (!c.has_layout<BytesObject>(args[i]) ||
            !is_subtype(c, c.type_of(args[i]), c.builtin(CoreObject::BytesType))) {
            type_error(c, U"code image arguments must be bytes", out);
            return;
        }
    for (auto i : {11U, 12U, 13U})
        if (!c.has_layout<StrObject>(args[i]) ||
            !is_subtype(c, c.type_of(args[i]), c.builtin(CoreObject::StrType))) {
            type_error(c, U"code filename, name and qualname must be strings", out);
            return;
        }
    if (!c.has_layout<TupleObject>(args[8]) ||
        !is_subtype(c, c.type_of(args[8]), c.builtin(CoreObject::TupleType))) {
        type_error(c, U"code constants must be a tuple", out);
        return;
    }
    CodeSpec spec;
    std::vector<std::u32string> names, variables;
    if (!string_tuple(c, args[9], names, out) || !string_tuple(c, args[10], variables, out) ||
        !string_tuple(c, args[17], spec.freevars, out) ||
        !string_tuple(c, args[18], spec.cellvars, out))
        return;
    try {
        const auto flags = numbers[6];
        if (numbers[2] > numbers[1] || numbers[4] != variables.size() ||
            static_cast<std::uint64_t>(numbers[1]) + numbers[3] + bool(flags & 4) +
                    bool(flags & 8) >
                variables.size())
            invalid_image("inconsistent code argument and local counts");
        spec.name = c.payload<StrObject>(args[12]).value;
        spec.qualname = c.payload<StrObject>(args[13]).value;
        spec.filename = c.payload<StrObject>(args[11]).value;
        spec.first_line = numbers[14];
        spec.generator = flags & (32 | 512);
        spec.coroutine = flags & (128 | 512);
        spec.nested = flags & 16;
        spec.future_flags = flags & supported_future_flags;
        for (std::size_t i = 0; i < static_cast<std::size_t>(numbers[1]) + numbers[3]; ++i)
            spec.signature.parameters.push_back(
                {variables[i], i < numbers[2]   ? ParameterKind::PositionalOnly
                               : i < numbers[1] ? ParameterKind::PositionalOrKeyword
                                                : ParameterKind::KeywordOnly});
        std::size_t parameter = static_cast<std::size_t>(numbers[1]) + numbers[3];
        if (flags & 4)
            spec.signature.varargs = variables[parameter++];
        if (flags & 8)
            spec.signature.varkwargs = variables[parameter++];

        ImageReader code(c, c.payload<BytesObject>(args[7]).value, bytecode_magic);
        spec.kind = static_cast<CodeKind>(code.byte());
        const auto local_count = code.count(4);
        for (std::size_t i = 0; i < local_count; ++i) {
            const auto slot = code.word();
            const auto &table = slot & 0x80000000U ? spec.cellvars : variables;
            const auto index = slot & 0x7fffffffU;
            if (index >= table.size())
                invalid_image("code local mapping is out of bounds");
            spec.locals.push_back(table[index]);
        }
        std::vector<Instruction> instructions;
        const auto instruction_count = code.count(5);
        instructions.reserve(instruction_count);
        for (std::size_t i = 0; i < instruction_count; ++i) {
            const auto opcode = static_cast<Opcode>(code.byte());
            instructions.push_back({opcode, code.word()});
        }
        std::vector<CallLayout> calls;
        std::vector<std::vector<std::u32string>> keywords;
        const auto call_count = code.count(8);
        for (std::size_t i = 0; i < call_count; ++i) {
            const auto positional = code.word(), count = code.count(4);
            calls.push_back({positional, count});
            auto &keys = keywords.emplace_back();
            for (std::size_t j = 0; j < count; ++j)
                keys.push_back(code.text());
        }
        const auto documented = code.byte();
        if (documented > 1)
            invalid_image("invalid native code docstring marker");
        if (documented)
            spec.docstring = code.text();
        code.finish();

        std::vector<SourcePosition> positions;
        const auto &locations = c.payload<BytesObject>(args[15]).value;
        if (locations.empty())
            positions.resize(instruction_count);
        else if (!locations.starts_with(line_magic)) {
            if (locations.starts_with("CXXL"))
                invalid_image("unsupported native code line table version");
            if (locations.size() % 2)
                invalid_image("truncated legacy code line table");
            std::int64_t current_line = spec.first_line;
            std::size_t address = 0;
            auto fill = [&](std::size_t until) {
                if (until > instruction_count || current_line < 0 ||
                    current_line > std::numeric_limits<std::uint32_t>::max())
                    invalid_image("legacy code line table is out of range");
                while (positions.size() < until) {
                    const auto line = static_cast<std::uint32_t>(current_line);
                    positions.push_back({line, 0, line, 0});
                    if ((positions.size() & 4095) == 0)
                        c.poll();
                }
            };
            for (std::size_t offset = 0; offset < locations.size(); offset += 2) {
                address += static_cast<unsigned char>(locations[offset]);
                fill(address);
                const auto delta = static_cast<unsigned char>(locations[offset + 1]);
                current_line +=
                    delta < 128 ? static_cast<int>(delta) : static_cast<int>(delta) - 256;
            }
            fill(instruction_count);
        } else {
            ImageReader lines(c, locations, line_magic);
            const auto origin = lines.word(), position_count = lines.count(16);
            if (position_count != instruction_count)
                invalid_image("code source-position count mismatch");
            const auto delta = static_cast<std::int64_t>(spec.first_line) - origin;
            auto line = [&] {
                auto value = static_cast<std::int64_t>(lines.word());
                if (value) {
                    value += delta;
                    if (value <= 0 || value > std::numeric_limits<std::uint32_t>::max())
                        invalid_image("code source line is out of range");
                }
                return static_cast<std::uint32_t>(value);
            };
            for (std::size_t i = 0; i < position_count; ++i) {
                const auto start = line(), column = lines.word(), end = line(),
                           end_column = lines.word();
                positions.push_back({start, column, end, end_column});
            }
            lines.finish();
        }
        std::vector<RegionLayout> regions;
        const auto &exceptions = c.payload<BytesObject>(args[16]).value;
        if (!exceptions.empty()) {
            ImageReader table(c, exceptions, region_magic);
            const auto count = table.count(38);
            for (std::size_t i = 0; i < count; ++i) {
                RegionLayout region{};
                region.kind = static_cast<RegionKind>(table.byte());
                const auto parent = table.byte();
                if (parent > 1)
                    invalid_image("invalid code region parent marker");
                if (parent) {
                    const auto index = table.word();
                    region.parent = RegionToken{index, static_cast<RegionPhase>(table.byte())};
                }
                region.enter_pc = table.word();
                region.leave_pc = table.word();
                region.handler_pc = table.word();
                region.end_handler_pc = table.word();
                region.after_pc = table.word();
                region.normal_pc = table.word();
                region.continue_pc = table.word();
                region.break_pc = table.word();
                region.depth = table.word();
                regions.push_back(region);
            }
            table.finish();
        }
        const auto constant_count = c.payload<TupleObject>(args[8]).items.size();
        auto verified = [&] {
            // Verification reads only copied host IR; invocation roots retain the inputs.
            NativeWait parked(c);
            return VerifiedCode::from_parts(spec, std::move(instructions), std::move(positions),
                                            std::move(calls), std::move(regions), constant_count,
                                            names.size(), numbers[5]);
        }();
        if (verified.flags() != flags)
            invalid_image("code flags are inconsistent with its native execution kind");
        RootScope roots(c);
        ReturnSlot temporary(c);
        std::vector<Value> keyword_tables;
        for (const auto &keys : keywords) {
            std::vector<Value> entries;
            for (const auto &key : keys) {
                make_str(c, key, temporary);
                entries.push_back(roots.root(temporary.value()).get(c));
            }
            cxxpython::core::make_tuple(c, entries, temporary);
            keyword_tables.push_back(roots.root(temporary.value()).get(c));
        }
        cxxpython::core::make_tuple(c, keyword_tables, temporary);
        const auto tables = roots.root(temporary.value());
        auto doc = roots.root(c.builtin(CoreObject::None));
        if (spec.docstring) {
            make_str(c, *spec.docstring, temporary);
            doc = roots.root(temporary.value());
        }
        c.allocate_instance<CodeObject>(roots.root(c.builtin(CoreObject::CodeType)), out,
                                        std::move(verified), args[8], args[10], args[12], args[13],
                                        args[11], tables.get(c), args[9], doc.get(c), args[18],
                                        args[17], args[7], args[15], args[16]);
    } catch (const CodeValidationError &error) {
        const std::string message(error.what());
        raise_error(c, CoreObject::ValueErrorType, std::u32string(message.begin(), message.end()),
                    out);
    }
}
void code_replace(Attachment &c, Value, std::span<const Value> args, ReturnSlot &out)
{
    if (c.type_of(args[0]) != c.builtin(CoreObject::CodeType)) {
        type_error(c, U"replace requires a code object", out);
        return;
    }
    constexpr std::array names{U"co_argcount",       U"co_posonlyargcount", U"co_kwonlyargcount",
                               U"co_nlocals",        U"co_stacksize",       U"co_flags",
                               U"co_code",           U"co_consts",          U"co_names",
                               U"co_varnames",       U"co_filename",        U"co_name",
                               U"co_qualname",       U"co_firstlineno",     U"co_linetable",
                               U"co_exceptiontable", U"co_freevars",        U"co_cellvars"};
    RootScope roots(c);
    const auto self = roots.root(args[0]);
    std::vector<Value> values{c.builtin(CoreObject::CodeType)};
    ReturnSlot name(c), value(c);
    for (const auto *field : names) {
        make_str(c, field, name);
        get_attribute(c, self, roots.root(name.value()), value);
        if (raised(value)) {
            out.raised(value.exception());
            return;
        }
        values.push_back(roots.root(value.value()).get(c));
    }
    DictView changes(c, args[1]);
    for (const auto &entry : changes.entries()) {
        const auto &key = c.payload<StrObject>(entry.key).value;
        const auto found = std::find_if(names.begin(), names.end(),
                                        [&](const auto *field) { return key == field; });
        if (found == names.end()) {
            type_error(c, U"'" + key + U"' is an invalid keyword argument for replace()", out);
            return;
        }
        values[static_cast<std::size_t>(found - names.begin()) + 1] = entry.value;
    }
    code_new(c, args[0], values, out);
}
} // namespace
std::array<std::string, 3> encode_code(Attachment &c, const VerifiedCode &program, Value variables,
                                       Value call_keywords, Value docstring)
{
    std::array<std::string, 3> result{std::string(bytecode_magic), std::string(line_magic), {}};
    auto &code = result[0], &lines = result[1], &exceptions = result[2];
    auto string = [&](std::string &out, std::u32string_view value) {
        word(out, value.size());
        for (std::size_t i = 0; i < value.size(); ++i) {
            word(out, value[i]);
            if ((i & 4095) == 0)
                c.poll();
        }
    };
    code.push_back(static_cast<char>(program.kind()));
    std::unordered_map<std::u32string_view, std::uint32_t> locals, cells;
    const auto &varnames = c.payload<TupleObject>(variables).items;
    for (std::size_t i = 0; i < varnames.size(); ++i)
        locals.emplace(c.payload<StrObject>(varnames[i]).value, static_cast<std::uint32_t>(i));
    for (std::size_t i = 0; i < program.cell_names().size(); ++i)
        cells.emplace(program.cell_names()[i], static_cast<std::uint32_t>(i));
    word(code, program.local_names().size());
    for (const auto &local : program.local_names()) {
        if (auto found = locals.find(local); found != locals.end())
            word(code, found->second);
        else if (auto found = cells.find(local); found != cells.end())
            word(code, found->second | 0x80000000U);
        else
            invalid_image("code local is absent from its reflection tables");
    }
    word(code, program.instructions().size());
    for (std::size_t i = 0; i < program.instructions().size(); ++i) {
        const auto instruction = program.instructions()[i];
        code.push_back(static_cast<char>(instruction.opcode));
        word(code, instruction.operand);
        if ((i & 4095) == 0)
            c.poll();
    }
    word(code, program.calls().size());
    const auto &tables = c.payload<TupleObject>(call_keywords).items;
    if (tables.size() != program.calls().size())
        invalid_image("code call-site keyword table mismatch");
    for (std::size_t i = 0; i < tables.size(); ++i) {
        const auto call = program.calls()[i];
        const auto &keys = c.payload<TupleObject>(tables[i]).items;
        if (keys.size() != call.keywords)
            invalid_image("code call-site keyword count mismatch");
        word(code, call.positional);
        word(code, call.keywords);
        for (auto key : keys)
            string(code, c.payload<StrObject>(key).value);
    }
    code.push_back(docstring != c.builtin(CoreObject::None));
    if (docstring != c.builtin(CoreObject::None))
        string(code, c.payload<StrObject>(docstring).value);
    word(lines, program.first_line());
    word(lines, program.positions().size());
    for (std::size_t i = 0; i < program.positions().size(); ++i) {
        const auto &position = program.positions()[i];
        word(lines, position.line);
        word(lines, position.column);
        word(lines, position.end_line);
        word(lines, position.end_column);
        if ((i & 4095) == 0)
            c.poll();
    }
    if (!program.regions().empty()) {
        exceptions = region_magic;
        word(exceptions, program.regions().size());
        for (std::size_t i = 0; i < program.regions().size(); ++i) {
            const auto &region = program.regions()[i];
            exceptions.push_back(static_cast<char>(region.kind));
            exceptions.push_back(region.parent.has_value());
            if (region.parent) {
                word(exceptions, region.parent->region);
                exceptions.push_back(static_cast<char>(region.parent->phase));
            }
            for (auto field :
                 {region.enter_pc, region.leave_pc, region.handler_pc, region.end_handler_pc,
                  region.after_pc, region.normal_pc, region.continue_pc, region.break_pc})
                word(exceptions, field);
            word(exceptions, region.depth);
            if ((i & 4095) == 0)
                c.poll();
        }
    }
    return result;
}
void install_code_descriptors(Attachment &c, std::span<const Handle> core)
{
    const auto owner = core[static_cast<std::size_t>(CoreObject::CodeType)];
    for (const auto &[name, getter] :
         std::initializer_list<std::pair<const char32_t *, NativeGetter>>{
             {U"co_name", code_name},
             {U"co_qualname", code_qualname},
             {U"co_filename", code_filename},
             {U"co_consts", code_constants},
             {U"co_varnames", code_varnames},
             {U"co_stacksize", code_stacksize},
             {U"co_firstlineno", code_firstline},
             {U"co_flags", code_flags},
             {U"co_argcount", code_argcount},
             {U"co_posonlyargcount", code_posonlycount},
             {U"co_kwonlyargcount", code_kwonlycount},
             {U"co_nlocals", code_nlocals},
             {U"co_names", code_names},
             {U"co_cellvars", code_cellvars},
             {U"co_freevars", code_freevars},
             {U"co_code", code_bytecode},
             {U"co_linetable", code_linetable},
             {U"co_exceptiontable", code_exceptiontable},
             {U"co_lnotab", code_lnotab}})
        install_native_field(c, core, owner, name, getter);
    SignatureLayout unary;
    unary.parameters = {{U"self", ParameterKind::PositionalOnly}};
    install_native_attribute(c, core, owner, U"co_positions", unary, code_locations<false>);
    install_native_attribute(c, core, owner, U"co_lines", unary, code_locations<true>);
    SignatureLayout constructor;
    for (const auto *name :
         {U"cls", U"argcount", U"posonlyargcount", U"kwonlyargcount", U"nlocals", U"stacksize",
          U"flags", U"codestring", U"constants", U"names", U"varnames", U"filename", U"name",
          U"qualname", U"firstlineno", U"linetable", U"exceptiontable"})
        constructor.parameters.push_back({name, ParameterKind::PositionalOnly});
    constructor.parameters.push_back(
        {U"freevars", ParameterKind::PositionalOnly,
         core[static_cast<std::size_t>(CoreObject::EmptyTuple)].get(c)});
    constructor.parameters.push_back(
        {U"cellvars", ParameterKind::PositionalOnly,
         core[static_cast<std::size_t>(CoreObject::EmptyTuple)].get(c)});
    install_native_attribute(c, core, owner, U"__new__", constructor, code_new);
    auto replace = unary;
    replace.varkwargs = U"kwargs";
    install_native_attribute(c, core, owner, U"replace", replace, code_replace);
}
} // namespace cxxpython::core::detail
