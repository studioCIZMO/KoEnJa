" Tells KoEnJa which mode Vim is in, so that every normal-mode key goes straight to Vim, Esc
" always reaches it, and words are decided only in insert mode.
" Put this file in ~/.vim/plugin/ (Vim 8.2 or later, with +job). Neovim: koenja.lua.
" From Araso's scripts/vim/araso.vim (MIT), for KoEnJa on macOS.
if exists('g:loaded_koenja') || !has('job') || has('nvim')
  finish
endif
let g:loaded_koenja = 1

let s:koenja = ''
for s:path in ['/Library/Input Methods/KoEnJa.app/Contents/MacOS/KoEnJa',
      \ expand('~/Library/Input Methods') . '/KoEnJa.app/Contents/MacOS/KoEnJa']
  if executable(s:path)
    let s:koenja = s:path
    break
  endif
endfor
if empty(s:koenja)
  finish
endif

" The app Vim runs in, so the mode reaches that app's text field and no other: macOS gives
" every app's processes its bundle identifier (none: taken where Esc isn't a switch key).
let s:app = exists('$__CFBundleIdentifier') ? $__CFBundleIdentifier : ''

function! s:Tell(mode) abort
  " Not stopped when Vim exits, so the last word still goes out when quitting.
  call job_start([s:koenja, '--vim', a:mode, '--app', s:app], {'stoponexit': ''})
endfunction

function! s:Current() abort
  return mode() =~# '^[iR]' ? 'insert' : 'normal'
endfunction

augroup KoEnJa
  autocmd!
  autocmd InsertEnter * call s:Tell('insert')
  autocmd InsertLeave,VimEnter * call s:Tell('normal')
  autocmd FocusGained * call s:Tell(s:Current())
  " Whatever is in front next (a shell in another pane) isn't Vim.
  autocmd VimLeavePre,FocusLost * call s:Tell('off')
  if exists('##VimResume')
    autocmd VimResume * call s:Tell('normal')
    autocmd VimSuspend * call s:Tell('off')
  endif
augroup END
