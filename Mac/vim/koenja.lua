-- Tells KoEnJa which mode neovim is in, so that every normal-mode key goes straight to neovim,
-- Esc always reaches it, and words are decided only in insert mode.
-- Put this file in ~/.config/nvim/plugin/ (or require it from init.lua). Vim: koenja.vim.
-- From Araso's scripts/vim/araso.lua (MIT), for KoEnJa on macOS.
local koenja
for _, path in ipairs({ '/Library/Input Methods/KoEnJa.app/Contents/MacOS/KoEnJa',
                        vim.fn.expand('~/Library/Input Methods') .. '/KoEnJa.app/Contents/MacOS/KoEnJa' }) do
  if vim.fn.executable(path) == 1 then
    koenja = path
    break
  end
end
if not koenja then return end

-- The app neovim runs in, so the mode reaches that app's text field and no other: macOS gives
-- every app's processes its bundle identifier (none: taken where Esc isn't a switch key).
local app = vim.env.__CFBundleIdentifier or ''

local function tell(mode)
  -- Detached, so the last word still goes out when neovim is quitting.
  vim.fn.jobstart({ koenja, '--vim', mode, '--app', app }, { detach = true })
end

local function current()
  local mode = vim.api.nvim_get_mode().mode
  return (mode:find('^[iR]') and 'insert') or 'normal'
end

local group = vim.api.nvim_create_augroup('KoEnJa', { clear = true })
local function on(events, mode)
  vim.api.nvim_create_autocmd(events, {
    group = group,
    callback = function() tell(type(mode) == 'function' and mode() or mode) end,
  })
end

on('InsertEnter', 'insert')
on({ 'InsertLeave', 'VimEnter', 'VimResume' }, 'normal')
on('FocusGained', current)
-- Whatever is in front next (a shell in another pane) isn't neovim.
on({ 'VimLeavePre', 'VimSuspend', 'FocusLost' }, 'off')
