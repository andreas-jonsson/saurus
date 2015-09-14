-- S A U R U S
-- Copyright (c) 2009-2015 Andreas T Jonsson <andreas@saurus.org>
--
-- This Source Code Form is subject to the terms of the Mozilla Public
-- License, v. 2.0. If a copy of the MPL was not distributed with this
-- file, You can obtain one at http://mozilla.org/MPL/2.0/.

local line_info = true

local tokens = {
    '(', ')', '{', '}', '[', ']',
    '~', '->', ';', '@', '.'
}

local call_prefix = {
    ')(', '}(', ']('
}

local operators = {
    '=', ':', '..',
    '|', '&',
    '<', '>', '<=', '>=', '~=', '==',
    '+', '-',
    '*', '/', '%',
    '^'
}

local keywords = {
    'do', 'if', 'else', 'for', 'def', 'include', 'macro', 'cfun', 'cdec', 'cinclude'
}

for _,v in ipairs(operators) do
    table.insert(tokens, v)
end

for _,v in ipairs(call_prefix) do
    table.insert(tokens, v)
end

local function is_call_prefix(tok)
    for _,v in ipairs(call_prefix) do
        if v == tok then
            return true
        end
    end
    return false
end

local function op_idx(op)
    for k,v in ipairs(operators) do
        if v == op then
            return k
        end
    end
    error('Invalid operator!')
end

local function parser_error_fl(file, line, ...)
    saurus_error = string.format(...) .. string.format(' error: %s:%i', file, line)
    --print(debug.traceback())
    error(saurus_error)
end

local function parser_error(stream, ...)
    parser_error_fl(stream.file, stream.line, ...)
end

local function parser_assert(cond, stream, ...)
    if not cond then
        parser_error_fl(stream.file, stream.line, ...)
    end
end

local function string_escape(stream, ch)
    if ch == '^' then
        ch = stream:get()
        if ch == 'n' then
            ch = '\n'
        elseif ch == 't' then
            ch = '\t'
        elseif ch == 'r' then
            ch = '\r'
        elseif ch == '0' then
            ch = '\0'
        elseif ch == '"' then
            ch = '"'
        elseif ch == "'" then
            ch = "'"
        elseif ch == '^' then
        end
    end
    return ch
end

local function read_xstring(stream)
    local ch1 = stream:get()
    local ch2 = stream:get()
    local ch3 = stream:get()

    if not ch1 or not ch2 or not ch3 or (ch1 .. ch2 .. ch3) ~= '\'\'\'' then
        stream:put(ch1)
        stream:put(ch2)
        stream:put(ch3)
        return
    end

    local tail = ''
    local str = ''
    while true do
        local ch = stream:get()
        if not ch then
            return
        elseif ch == "'" then
            tail = tail .. ch
            if #tail == 3 then
                return string.sub(str, 1, -3)
            end
        else
            tail = ''
        end
        str = str .. string_escape(stream, ch)
    end
end

local function read_string(stream)
    local str = ''
    while true do
        local ch = stream:get()
        if not ch then
            return
        elseif ch == '"' or ch == '\n' then
            return str
        end
        str = str .. string_escape(stream, ch)
    end
end

local function read_comment(stream)
    while true do
        local ch = stream:get()
        if not ch or ch == '\n' then
            return
        end
    end
end

local function read_space(stream)
    while true do
        local ch = stream:get()
        if not ch then
            return
        elseif not string.find(ch, '[%s%c]') then
            stream:put(ch)
            return
        end
    end
end

local function is_key(str)
    for _,k in ipairs(keywords) do
        if k == str then
            return true
        end
    end
    return false
end

local function read_identifier(stream)
    local prev
    local str = ''
    while true do
        local ch = stream:get()
        if ((not prev and string.find(ch, '[%d]'))) or (not string.find(ch, '[%w_!%?]')) then
            stream:put(ch)
            return str, (ch == '('), is_key(str)
        end
        str = str .. ch
        prev = ch
    end
end

local function read_number(stream)
    local prev
    local num = ''
    local buffer = {}

    while true do
        local ch = stream:get()
        if (prev == '.' or prev == '-') and ch == '.' then
            table.remove(buffer, #buffer)
            stream:put(prev)
            stream:put(ch)
            ch = nil
        elseif not prev and ch == '.' then
            stream:put(ch)
            ch = nil
        elseif prev and ch == '-' then
            stream:put(ch)
            ch = nil
        else
            table.insert(buffer, ch)
            prev = ch
        end

        if not ch or not string.find(ch, '[%d%-.]') then
            num = tonumber(num)
            if num then
                stream:put(ch)
            else
                while #buffer > 0 do
                    stream:put(table.remove(buffer, #buffer))
                end
            end
            return num
        end
        num = num .. ch
    end
end

function revert_token(stream, tok)
    if tok then
        table.insert(stream._reverted, tok)
    end
end

function next_token(stream)
    if #stream._reverted > 0 then
        return table.remove(stream._reverted, 1)
    end

    local ch
    repeat
        read_space(stream)
        ch = stream:get()
        if not ch then
            return
        end
        if ch == '#' then
            read_comment(stream)
        end
    until ch ~= '#'

    local line = stream.line
    if ch == '"' then
        local str = read_string(stream)
        if str then
            return {value = str, file = stream.file, line = line}
        else
            parser_error_fl(stream.file, stream.line, 'unterminated string')
        end
    end

    local op
    for _,k in ipairs(tokens) do
        if k == ch then
            op = k
            break
        end
    end

    local op2
    local ch2 = stream:get()
    for _,k in ipairs(tokens) do
        local tok = ch .. (ch2 or '')
        if k == tok then
            op2 = k
            break
        end
    end

    local is_call = false
    if not op2 then
        stream:put(ch2)
    elseif is_call_prefix(op2) then
        is_call = true
    end

    op = op2 or op
    if op then
        if op == '-' then
            stream:put(op)
            local num = read_number(stream)
            if num then
                return {value = num, file = stream.file, line = line}
            end
            stream:get()
        end
        return {token = op, is_call = is_call, file = stream.file, line = line}
    end

    stream:put(ch)
    local num = read_number(stream)
    if num then
        return {value = num, file = stream.file, line = line}
    else
        local identifier, is_call, is_key = read_identifier(stream)
        if identifier == '' then
            local str = read_xstring(stream)
            if str then
                return {value = str, file = stream.file, line = line}
            end
            parser_error(stream, 'invalid identifier')
        end
        if identifier == 'true' or identifier == 'false' then
            return {value = (identifier == 'true'), file = stream.file, line = line}
        else
            return {value = {identifier}, is_call = is_call, is_key = is_key, file = stream.file, line = line}
        end
    end
end

-----------------------------------------------------------

local function is_operator(token)
    for _,v in ipairs(operators) do
        if v == token then
            return true
        end
    end
    return false
end

local function token_to_identifier(tok)
    assert(type(tok.value) == 'table')
    return {type = 'identifier', value = tok.value[1], file = tok.file, line = tok.line}
end

local function args_to_identifiers(args)
    local ident = {}
    for _,tok in ipairs(args) do
        table.insert(ident, token_to_identifier(tok))
    end
    return ident
end

local function read_dot(stream, left)
    local path = {}
    local prev
    local tok
    local is_call

    while true do
        tok = next_token(stream)
        if tok and type(tok.value) == 'table' and (not prev or prev == '.') then
            local str = {type = 'string', value = tok.value[1], file = tok.file, line = tok.line}
            table.insert(path, str)
            prev = tok.value[1]
            is_call = tok.is_call
        elseif tok and prev and prev ~= '.' and tok.token == '.' then
            prev = tok.token
        else
            revert_token(stream, tok)
            break
        end
    end

    parser_assert(#path > 0, stream, 'invalid index operation')
    local dot = {type = 'dot', operator = op, left = left, path = path, file = stream.file, line = stream.line}
    if tok and is_call then
        dot = read_call(stream, dot)
    end

    if tok then
        tok = next_token(stream)
        if tok and is_operator(tok.token) then
            return read_operator(stream, dot, tok.token)
        else
            revert_token(stream, tok)
        end
    end
    return dot
end

function read_operator(stream, left, op)
    if op == '=' and left.type ~= 'identifier' then
        parser_error(stream, 'assignment expected identifier')
    end

    local msg = 'operator expected right side expression'
    local expr = read_expression(stream, false)

    parser_assert(expr, stream, msg)

    local operation = {left, op, expr}
    while true do
        local tok = next_token(stream)
        if tok and is_operator(tok.token) then
            local expr = read_expression(stream, false)
            parser_assert(expr, stream, msg)
            table.insert(operation, tok.token)
            table.insert(operation, expr)
        else
            revert_token(stream, tok)
            break
        end
    end

    assert(#operation >= 3 and #operation % 2 == 1)

    for oi = #operators, 1, -1 do
        local o = operators[oi]
        local i = 2
        while i <= #operation - 1 do
            if o == operation[i] then
                local right = table.remove(operation, i + 1)
                local left = table.remove(operation, i - 1)
                operation[i - 1] = {type = 'operator', operator = o, left = left, right = right, file = left.file, line = left.line}
                i = 2
            else
                i = i + 2
            end
        end
    end

    assert(#operation == 1)
    return operation[1]
end

function read_call(stream, func)
    local tok = next_token(stream)
    assert(tok.token == '(' or is_call_prefix(tok.token))

    local line = stream.line
    local arguments = {}
    local tok_end

    while true do
        tok_end = next_token(stream)
        if not tok_end then
            parser_error(stream, 'unterminated argument list')
        elseif tok_end.token == ')' or is_call_prefix(tok_end.token) then
            break
        end
        revert_token(stream, tok_end)
        local expr = read_expression(stream)
        if not tok_end then
            parser_error(stream, 'unexpected end of expression')
        end
        table.insert(arguments, expr)
    end

    local f = {type = 'call', func = func, args = arguments, file = stream.file, line = line}
    if tok_end and is_call_prefix(tok_end.token) then
        revert_token(stream, tok_end)
        return read_call(stream, f)
    else
        return f
    end
end

local function read_def(stream)
    local message = 'incompleat definition'
    local tok = next_token(stream)
    if not tok or type(tok.value) ~= 'table' then
        parser_error(stream, message)
    end

    local name = tok.value[1]
    tok = next_token(stream)
    if not tok or tok.token ~= '=' then
        parser_error(stream, message)
    end

    local value = read_expression(stream)
    if not value then
        parser_error(stream, message)
    end
    return {type = 'def', name = name, value = value, file = stream.file, line = stream.line}
end

local function check_c_guard(code)
    for _,c in ipairs(c_guard) do
        if c == code then
            return false
        end
    end
    table.insert(c_guard, code)
    return true
end

local function read_cdec(stream)
    local message = 'incompleat C declaration'
    local tok = next_token(stream)
    if not tok or type(tok.value) ~= 'string' then
        parser_error(stream, message)
    end

    if check_c_guard(tok.value) then
        line_inf = line_info and string.format('\n#ifdef SU_OPT_C_LINE\n\t#line %i "%s"\n#endif', math.max(tok.line - 1, 1), tok.file) or ''
        c_code = string.format('%s%s\n%s\n\n', (c_code or ''), line_inf, tok.value)
    end

    return {type = 'identifier', value = 'nil', file = tok.file, line = tok.line}
end

local function is_enum(name)
    if string.match(name, '[%w_]+') == name then
        return name
    end
end

local function read_cfun(stream)
    local message = 'incompleat C definition'
    local tok = next_token(stream)
    if not tok or type(tok.value) ~= 'string' then
        parser_error(stream, message)
    end

    --if check_c_guard(tok.value) then
        local enum = is_enum(tok.value)
        local name = "clambda" .. tostring(#c_func)
        table.insert(c_func, {name = name, enum = enum})
        if not enum then
            line_inf = line_info and string.format('\n#ifdef SU_OPT_C_LINE\n\t#line %i "%s"\n#endif', math.max(tok.line - 1, 1), tok.file) or ''
            c_code = string.format('%sstatic int ___%s(su_state *s, int narg) {\nconst int ___top = su_top(s);\n{%s\n%s\n}\nreturn (su_top(s) - ___top) > 0 ? 1 : 0;\n}\n\n', (c_code or ''), name, line_inf, tok.value)
        end
    --end

    return {type = 'clambda', id = #c_func - 1, file = tok.file, line = tok.line}
end

local function read_for(stream)
    local message = 'incompleat for loop'
    local tok = next_token(stream)
    if not tok or type(tok.value) ~= 'table' then
        parser_error(stream, message)
    end

    local name = tok.value[1]
    tok = next_token(stream)
    if not tok or tok.token ~= '=' then
        parser_error(stream, message)
    end

    local value = read_expression(stream)
    if not value then
        parser_error(stream, message)
    end

    local body = read_expression(stream)
    if not body then
        parser_error(stream, message)
    end
    return {type = 'for', name = name, value = value, body = body, file = stream.file, line = stream.line}
end

local function read_include(stream)
    local name = read_expression(stream)
    if not name or type(name.value) ~= 'string' then
        parser_error(stream, 'expected filename')
    end

    local src = stream._path .. '/' .. name.value .. '.su'
    local fpath = get_file_path(src)
    local fp = io.open(src, 'rb')
    if not fp then
        local path = os.getenv('saurus') or '/opt/saurus'
        src = path .. '/include/' .. name.value .. '.su'
        fpath = get_file_path(src)
        fp = io.open(src, 'rb')
        if not fp then
            parser_error(stream, "can't open: %s.su", name.value)
        end
    end

    local sub_stream = create_stream(fp, src, fpath)
    local ast = read_block(sub_stream)
    fp:close()

    ast = {type = 'lambda', args = {}, body = ast, varg = nil, file = ast.file, line = ast.line}
    return {type = 'call', func = ast, args = {}, file = ast.file, line = ast.line}
end

local function read_cinclude(stream)
    local name = read_expression(stream)
    if not name or type(name.value) ~= 'string' then
        parser_error(stream, 'expected filename')
    end

    local src = stream._path .. '/' .. name.value
    local fp = io.open(src, 'r')
    if not fp then
        parser_error(stream, "can't open: %s", src)
    end

    local code = fp:read('*a')
    fp:close()

    c_code = string.format('%s#ifdef SU_OPT_C_LINE\n\t#line 1 "%s"\n%s\n#endif\n', (c_code or ''), src, code)
    return {type = 'identifier', value = 'nil', file = name.file, line = name.line}
end

function read_block(stream)
    local block = {}
    local line = stream.line
    while true do
        local tok = next_token(stream)
        if not tok or tok.token == ';' then
            break
        end
        revert_token(stream, tok)

        local expr = read_expression(stream)
        if not expr then
            break
        end
        table.insert(block, expr)
    end
    if #block == 0 then
        parser_error(stream, 'empty body')
    end
    return {type = 'block', body = block, file = stream.file, line = line}
end

local function read_collection(stream, type)
    local end_tok = (type == 'vector') and ']' or '}'
    local end_tok_call = (type == 'vector') and '](' or '}('
    local line = stream.line
    local arguments = {}
    local is_call = false

    while true do
        local tok = next_token(stream)
        if not tok then
            parser_error(stream, 'unterminated argument list')
        elseif tok.token == end_tok then
            break
        elseif tok.token == end_tok_call then
            revert_token(stream, tok)
            is_call = true
            break
        end
        revert_token(stream, tok)
        local expr = read_expression(stream)
        if not tok then
            parser_error(stream, 'unexpected end of expression')
        end

        if type == 'hashmap' then
            if expr.type ~= 'operator' or (expr.operator ~= ':' and expr.operator ~= '=') then
                parser_error(stream, 'hashmap expected key-value pairs')
            end
            if expr.operator == '=' then
                expr.left = {type = 'string', value = expr.left.value, file = expr.file, line = expr.line}
            end
            table.insert(arguments, expr.left)
            table.insert(arguments, expr.right)
        else
            table.insert(arguments, expr)
        end
    end

    local ret = {type = 'collection', collection = type, args = arguments, file = stream.file, line = line}
    if is_call then
        return read_call(stream, ret)
    end
    return ret
end

local function read_cond(stream)
    local message = 'incompleat statement'
    local cond = read_expression(stream)
    if not cond then
        parser_error(stream, message)
    end

    local _if = read_expression(stream)
    if not _if then
        parser_error(stream, message)
    end

    local _else
    local tok = next_token(stream)
    if tok and tok.is_key and tok.value[1] == 'else' then
        _else = read_expression(stream)
        if not _else then
            parser_error(stream, message)
        end
    else
        revert_token(stream, tok)
    end

    return {type = 'cond', cond = cond, _if = _if, _else = _else, file = stream.file, line = line}
end

local function read_lambda(stream, varg)
    local line = stream.line
    local tokens, arguments = {}, {}

    if varg then
        local tok = next_token(stream)
        table.insert(tokens, tok)
        table.insert(arguments, tok)
    else
        while true do
            local tok = next_token(stream)
            table.insert(tokens, tok)
            if not tok or type(tok.value) ~= 'table' then
                if not tok or tok.token ~= ')' then
                    while #tokens > 0 do
                        revert_token(stream, table.remove(tokens, 1))
                    end
                    return
                end
                break
            end
            table.insert(arguments, tok)
        end
    end

    local tok = next_token(stream)
    if not tok or tok.token ~= '->' then
        while #tokens > 0 do
            revert_token(stream, table.remove(tokens, 1))
        end
        revert_token(stream, tok)
        return
    end

    local block = read_block(stream)
    if not block then
        while #tokens > 0 do
            revert_token(stream, table.remove(tokens, 1))
        end
        revert_token(stream, tok)
    end

    return {type = 'lambda', args = args_to_identifiers(arguments), body = block, varg = varg, file = tok.file, line = line}
end

function read_expression(stream, read_op)
    local ret
    local tok = next_token(stream)

    if read_op == nil then
        read_op = true
    end

    local function flip(expr, ret)
        if expr.type == 'operator' and expr.operator ~= '.' then
            local left = expr.left
            ret.arg.left = ret
            ret.arg = left
            ret = expr
        end
        return ret
    end

    if not tok then
        return
    end

    local vtype = type(tok.value)
    if vtype == 'boolean' or vtype == 'number' or vtype == 'string' then
        ret = {type = vtype, value = tok.value, file = tok.file, line = tok.line}
    elseif vtype == 'table' then
        if tok.is_key then
            local v = tok.value[1]
            if v == 'do' then
                ret = read_block(stream)
            elseif v == 'if' then
                ret = read_cond(stream)
            elseif v == 'def' then
                ret = read_def(stream)
            elseif v == 'cfun' then
                ret = read_cfun(stream)
            elseif v == 'cdec' then
                ret = read_cdec(stream)
            elseif v == 'cinclude' then
                ret = read_cinclude(stream)
            elseif v == 'for' then
                ret = read_for(stream)
            elseif v == 'include' then
                ret = read_include(stream)
            elseif v == 'macro' then
                parser_error(stream, 'macros are not implemented')
            else
                parser_error(stream, 'invalid use of keyword: ' .. tok.value[1])
            end
        else
            local tmp = next_token(stream)
            if tmp and tmp.token == '->' then
                revert_token(stream, tok)
                revert_token(stream, tmp)
                ret = read_lambda(stream, true)
            else
                revert_token(stream, tmp)
                ret = token_to_identifier(tok)
                if tok.is_call then
                    ret = read_call(stream, ret)
                end
            end
        end
    elseif tok.token == '~' then
        local expr = read_expression(stream)
        if not expr then
            parser_error(stream, 'expected expression')
        end
        ret = {type = 'not', arg = expr, file = tok.file, line = tok.line}
        ret = flip(expr, ret)
    elseif tok.token == '-' then
        local expr = read_expression(stream)
        if not expr then
            parser_error(stream, 'expected expression')
        end
        ret = {type = 'unm', arg = expr, file = tok.file, line = tok.line}
        ret = flip(expr, ret)
    elseif tok.token == '@' then
        local expr = read_expression(stream)
        if not expr then
            parser_error(stream, 'expected expression')
        end
        ret = {type = 'unref', arg = expr, file = tok.file, line = tok.line}
        ret = flip(expr, ret)
    elseif tok.token == '{' then
        ret = read_collection(stream, 'hashmap')
    elseif tok.token == '[' then
        ret = read_collection(stream, 'vector')
    elseif tok.token == '(' then
        ret = read_lambda(stream)
        if not ret then
            ret = read_expression(stream)
            local tok2 = next_token(stream)
            if not tok2 or (tok2.token ~= ')' and not is_call_prefix(tok2.token)) then
                parser_error(stream, "expected ')'")
            end
            if tok2.is_call then
                revert_token(stream, tok2)
                ret = read_call(stream, ret)
            end
        end
    else
        parser_error(stream, "unexpected token '%s'", tok.token or tok.value)
    end

    local tok2 = next_token(stream)
    if not tok2 then
        return ret
    elseif tok2.token == '.' then
        return read_dot(stream, ret)
    elseif not is_operator(tok2.token) or not read_op then
        revert_token(stream, tok2)
        return ret
    end

    return read_operator(stream, ret, tok2.token)
end

-----------------------------------------------------------

function create_stream(input, name, path)
    local tab = {
        put = function(self, ch)
            if ch == '\n' then
                self.line = self.line - 1
            end
            table.insert(self._cache, ch)
        end,
        line = 1,
        file = name or '?',
        _source = input,
        _path = path,
        _offset = 1,
        _reverted = {},
        _cache = {}
    }

    if io.type(input) == 'file' then
        tab.get = function(self)
            local ch
            if #self._cache > 0 then
                ch = table.remove(self._cache, #self._cache)
            else
                ch = self._source:read(1)
            end
            if ch == '\n' then
                self.line = self.line + 1
            end
            return ch
        end
    elseif type(input) == 'string' then
        tab.get = function(self)
            local ch
            if #self._cache > 0 then
                ch = table.remove(self._cache, #self._cache)
            else
                if self._offset > #self._source then
                    return
                end
                ch = string.sub(self._source, self._offset, self._offset)
                self._offset = self._offset + 1
            end
            if ch == '\n' then
                self.line = self.line + 1
            end
            return ch
        end
    else
        error("Can't create stream from: " .. type(input))
    end

    return tab
end

function ast_print(node, lv)
    local function puts(lv, ...)
        for i=1, lv do
            io.write('\t')
        end
        print(...)
    end

    lv = (lv or -1) + 1
    if node.type == 'number' or node.type == 'boolean' or node.type == 'string' then
        puts(lv, 'value: ' .. tostring(node.value))
    elseif node.type == 'identifier' then
        puts(lv, 'identifier: ' .. node.value)
    elseif node.type == 'lambda' then
        puts(lv, 'lambda: (...) ->')
        ast_print(node.body, lv)
    elseif node.type == 'call' then
        puts(lv, string.format('%s:', node.type))
        ast_print(node.func, lv)
        puts(lv, '(')
        for _,v in ipairs(node.args) do
            ast_print(v, lv)
        end
        puts(lv, ')')
    elseif node.type == 'collection' then
        puts(lv, string.format('%s:', node.collection))
        puts(lv, '(')
        for _,v in ipairs(node.args) do
            ast_print(v, lv)
        end
        puts(lv, ')')
    elseif node.type == 'operator' then
        puts(lv, 'operator: ' .. node.operator)
        puts(lv, 'left:')
        ast_print(node.left, lv)
        puts(lv, 'right:')
        ast_print(node.right, lv)
    elseif node.type == 'cond' then
        puts(lv, 'if:')
        ast_print(node.cond, lv)
        puts(lv, 'then:')
        ast_print(node._if, lv)
        if node._else then
            puts(lv, 'else:')
            ast_print(node._else, lv)
        end
    elseif node.type == 'block' then
        puts(lv, 'block:')
        for _,v in ipairs(node.body) do
            ast_print(v, lv)
        end
    elseif node.type == 'def' then
        puts(lv, string.format('def %s:', node.name))
        ast_print(node.value, lv)
    elseif node.type == 'seq' or node.type == 'unm' or node.type == 'not' then
        puts(lv, string.format('%s:', node.type))
        ast_print(node.arg, lv)
    else
        error(node.type)
    end
end
