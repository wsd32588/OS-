/**
 * Prompt palette (Client half).
 *
 * Adds one compact button to the composer tool row. The button opens a small
 * panel with reusable prompts, user-added favourites and the last few prompts
 * sent in this Session. Choosing an entry inserts it at the caret through the
 * composer's public InputActions — the draft is never submitted from here.
 */
window.__ModuleLoader__.load({
  id: '@local/dsh-prompt-palette',
  factory(require) {
    const React = require('react');
    const h = React.createElement;
    const { useCallback, useEffect, useMemo, useRef, useState } = React;

    const NS = 'promptPalette';
    const STORAGE_KEY = 'dsh.prompt-palette.custom.v1';
    const STYLE_ID = '@local/dsh-prompt-palette/prompt-palette.css';
    const MAX_CUSTOM = 20;
    const MAX_RECENT = 5;
    const PREVIEW_CHARS = 64;

    const CSS = `
.dpp-root { position: relative; display: inline-flex; align-items: center; }
.dpp-trigger {
  width: 28px; height: 28px; flex: none; display: grid; place-items: center;
  padding: 0; border: none; border-radius: 999px; cursor: pointer;
  color: var(--dsw-alias-label-secondary); background: transparent;
}
.dpp-trigger:hover { background: var(--dsw-alias-bg-layer-2); color: var(--dsw-alias-label-primary); }
.dpp-trigger:focus-visible, .dpp-item:focus-visible, .dpp-del:focus-visible, .dpp-add:focus-visible, .dpp-input:focus-visible {
  outline: 2px solid var(--dsw-alias-brand-primary); outline-offset: 1px;
}
.dpp-trigger svg { width: 17px; height: 17px; display: block; }
.dpp-panel {
  position: absolute; left: 0; bottom: calc(100% + 10px); z-index: 40;
  box-sizing: border-box; width: 304px; max-width: min(304px, calc(100vw - 48px));
  max-height: min(60vh, 430px); overflow-y: auto;
  display: flex; flex-direction: column; gap: 2px;
  padding: 8px; border-radius: 10px;
  background: var(--dsw-alias-bg-overlay);
  border: 1px solid var(--dsw-alias-border-l1);
  color: var(--dsw-alias-label-primary);
  font-size: 12px; line-height: 18px;
}
.dpp-head { padding: 2px 8px 6px; }
.dpp-title { font-size: 12px; font-weight: 600; color: var(--dsw-alias-label-primary); }
.dpp-hint { color: var(--dsw-alias-label-secondary); font-size: 11px; line-height: 16px; }
.dpp-section {
  padding: 6px 8px 2px; font-size: 11px; font-weight: 600;
  color: var(--dsw-alias-brand-primary);
}
.dpp-row { display: flex; align-items: center; gap: 2px; }
.dpp-item {
  flex: 1 1 auto; min-width: 0; display: block; text-align: left;
  padding: 5px 8px; border: none; border-radius: 6px; cursor: pointer;
  background: transparent; color: var(--dsw-alias-label-primary);
  font: inherit; white-space: nowrap; overflow: hidden; text-overflow: ellipsis;
}
.dpp-item:hover { background: var(--dsw-alias-bg-layer-2); }
.dpp-del {
  flex: none; width: 22px; height: 22px; display: grid; place-items: center;
  padding: 0; border: none; border-radius: 6px; cursor: pointer;
  background: transparent; color: var(--dsw-alias-label-secondary); font-size: 14px; line-height: 1;
}
.dpp-del:hover { background: var(--dsw-alias-bg-layer-2); color: var(--dsw-alias-state-error-primary); }
.dpp-empty { padding: 2px 8px 4px; color: var(--dsw-alias-label-secondary); font-size: 11px; }
.dpp-add { display: flex; gap: 4px; padding: 6px 8px 2px; }
.dpp-input {
  flex: 1 1 auto; min-width: 0; height: 26px; box-sizing: border-box;
  padding: 0 8px; border-radius: 6px; font: inherit;
  color: var(--dsw-alias-label-primary);
  background: var(--dsw-alias-bg-layer-1);
  border: 1px solid var(--dsw-alias-border-l1);
}
.dpp-input::placeholder { color: var(--dsw-alias-label-secondary); }
.dpp-addbtn {
  flex: none; height: 26px; padding: 0 10px; border-radius: 6px; cursor: pointer;
  border: none; font: inherit; font-weight: 600;
  color: var(--dsw-alias-label-primary);
  background: var(--dsw-alias-bg-layer-2);
}
.dpp-addbtn:hover { background: var(--dsw-alias-border-l1); }
.dpp-addbtn:disabled { opacity: 0.5; cursor: default; }
.dpp-notice { padding: 4px 8px; color: var(--dsw-alias-state-warn-primary); font-size: 11px; line-height: 16px; }
`;

    function installStyle() {
      if (typeof document === 'undefined') return () => {};
      const existing = document.querySelector('style[data-plugin-css=' + JSON.stringify(STYLE_ID) + ']');
      if (existing !== null) return () => {};
      const tag = document.createElement('style');
      tag.dataset.plugin = '@local/dsh-prompt-palette';
      tag.dataset.pluginCss = STYLE_ID;
      tag.textContent = CSS;
      document.head.appendChild(tag);
      return () => tag.remove();
    }

    function loadCustom() {
      try {
        const raw = window.localStorage.getItem(STORAGE_KEY);
        if (raw === null) return [];
        const parsed = JSON.parse(raw);
        if (!Array.isArray(parsed)) return [];
        return parsed
          .filter((value) => typeof value === 'string' && value.trim() !== '')
          .map((value) => value.trim())
          .slice(0, MAX_CUSTOM);
      } catch {
        return [];
      }
    }

    function saveCustom(items) {
      try {
        window.localStorage.setItem(STORAGE_KEY, JSON.stringify(items));
      } catch {
        // A full or unavailable storage must not break the composer control.
      }
    }

    function contentText(content) {
      if (!Array.isArray(content)) return '';
      const parts = [];
      for (const block of content) {
        if (block !== null && typeof block === 'object' && block.type === 'text' && typeof block.text === 'string') {
          const text = block.text.trim();
          if (text !== '') parts.push(text);
        }
      }
      return parts.join('\n\n');
    }

    function preview(text) {
      const flat = text.replace(/\s+/g, ' ').trim();
      return flat.length > PREVIEW_CHARS ? flat.slice(0, PREVIEW_CHARS) + '…' : flat;
    }

    function collectRecent(nodes) {
      const out = [];
      if (!Array.isArray(nodes)) return out;
      for (let index = nodes.length - 1; index >= 0 && out.length < MAX_RECENT; index -= 1) {
        const node = nodes[index];
        if (node === null || typeof node !== 'object' || node.kind !== 'user') continue;
        const text = contentText(node.content);
        if (text === '' || out.includes(text)) continue;
        out.push(text);
      }
      return out;
    }

    function PromptRow({ text, label, onInsert, onRemove, removeLabel, keepFocus }) {
      return h(
        'div',
        { className: 'dpp-row' },
        h(
          'button',
          {
            type: 'button',
            className: 'dpp-item',
            title: text,
            onMouseDown: keepFocus,
            onClick: () => onInsert(text),
          },
          label,
        ),
        onRemove === undefined
          ? null
          : h(
              'button',
              {
                type: 'button',
                className: 'dpp-del',
                title: removeLabel,
                'aria-label': removeLabel,
                onMouseDown: keepFocus,
                onClick: onRemove,
              },
              '×',
            ),
      );
    }

    function PromptPanel({ useChat, t, custom, onAdd, onRemove, onInsert, keepFocus, notice }) {
      const nodes = useChat((snapshot) => snapshot.legacy.nodes);
      const recent = useMemo(() => collectRecent(nodes), [nodes]);
      const [draft, setDraft] = useState('');
      const submitAdd = useCallback(() => {
        const value = draft.trim();
        if (value === '') return;
        onAdd(value);
        setDraft('');
      }, [draft, onAdd]);
      const defaults = [
        t('prompt.continue'),
        t('prompt.explain'),
        t('prompt.plan'),
        t('prompt.test'),
        t('prompt.summary'),
        t('prompt.memory'),
      ];
      return h(
        'div',
        { className: 'dpp-panel', role: 'dialog', 'aria-label': t('open'), 'aria-modal': 'false' },
        h(
          'div',
          { className: 'dpp-head' },
          h('div', { className: 'dpp-title' }, t('title')),
          h('div', { className: 'dpp-hint' }, t('hint')),
        ),
        h('div', { className: 'dpp-section' }, t('favorites')),
        defaults.map((text) => h(PromptRow, { key: 'default:' + text, text, label: preview(text), onInsert, keepFocus })),
        custom.map((text) =>
          h(PromptRow, {
            key: 'custom:' + text,
            text,
            label: preview(text),
            onInsert,
            onRemove: () => onRemove(text),
            removeLabel: t('remove'),
            keepFocus,
          }),
        ),
        custom.length === 0 ? h('div', { className: 'dpp-empty' }, t('emptyCustom')) : null,
        h(
          'div',
          { className: 'dpp-add' },
          h('input', {
            className: 'dpp-input',
            type: 'text',
            value: draft,
            placeholder: t('addPlaceholder'),
            'aria-label': t('addPlaceholder'),
            onChange: (event) => setDraft(event.target.value),
            onKeyDown: (event) => {
              if (event.key === 'Enter') {
                event.preventDefault();
                submitAdd();
              }
            },
          }),
          h(
            'button',
            {
              type: 'button',
              className: 'dpp-addbtn',
              disabled: draft.trim() === '',
              onClick: submitAdd,
            },
            t('add'),
          ),
        ),
        recent.length === 0
          ? null
          : h(
              React.Fragment,
              null,
              h('div', { className: 'dpp-section' }, t('recent')),
              recent.map((text) =>
                h(PromptRow, { key: 'recent:' + text, text, label: preview(text), onInsert, keepFocus }),
              ),
            ),
        notice === null ? null : h('div', { className: 'dpp-notice' }, notice),
      );
    }

    function PromptPalette({ useChat, inputActions, t }) {
      const [open, setOpen] = useState(false);
      const [custom, setCustom] = useState(loadCustom);
      const [notice, setNotice] = useState(null);
      const rootRef = useRef(null);

      useEffect(() => {
        if (!open) return undefined;
        const onPointerDown = (event) => {
          const root = rootRef.current;
          if (root !== null && event.target instanceof Node && root.contains(event.target)) return;
          setOpen(false);
        };
        const onKeyDown = (event) => {
          if (event.key === 'Escape') setOpen(false);
        };
        document.addEventListener('pointerdown', onPointerDown, true);
        document.addEventListener('keydown', onKeyDown);
        return () => {
          document.removeEventListener('pointerdown', onPointerDown, true);
          document.removeEventListener('keydown', onKeyDown);
        };
      }, [open]);

      const keepFocus = useCallback((event) => {
        event.preventDefault();
      }, []);

      const insert = useCallback(
        (text) => {
          if (inputActions === undefined || inputActions === null) return;
          const inserted = inputActions.insertText(text, inputActions.captureInsertion());
          if (inserted) {
            setNotice(null);
            setOpen(false);
          } else {
            setNotice(t('insertFailed'));
          }
        },
        [inputActions, t],
      );

      const add = useCallback((value) => {
        setCustom((current) => {
          const next = current.includes(value) ? current : current.concat(value).slice(-MAX_CUSTOM);
          saveCustom(next);
          return next;
        });
      }, []);

      const remove = useCallback((value) => {
        setCustom((current) => {
          const next = current.filter((item) => item !== value);
          saveCustom(next);
          return next;
        });
      }, []);

      const trigger = h(
        'button',
        {
          type: 'button',
          className: 'dpp-trigger',
          title: t('open'),
          'aria-label': t('open'),
          'aria-haspopup': 'dialog',
          'aria-expanded': open,
          onMouseDown: keepFocus,
          onClick: () => {
            setNotice(null);
            setOpen((value) => !value);
          },
        },
        h(
          'svg',
          { viewBox: '0 0 16 16', width: 17, height: 17, 'aria-hidden': true },
          h('path', {
            d: 'M3 4.5h10M3 8h7M3 11.5h5',
            fill: 'none',
            stroke: 'currentColor',
            'stroke-width': 1.4,
            'stroke-linecap': 'round',
          }),
          h('circle', { cx: 12.2, cy: 11.2, r: 2.6, fill: 'none', stroke: 'currentColor', 'stroke-width': 1.2 }),
          h('path', { d: 'M12.2 10.1v2.2M11.1 11.2h2.2', stroke: 'currentColor', 'stroke-width': 1.1, 'stroke-linecap': 'round' }),
        ),
      );

      return h(
        'div',
        { className: 'dpp-root', ref: rootRef },
        trigger,
        open
          ? h(PromptPanel, {
              useChat,
              t,
              custom,
              onAdd: add,
              onRemove: remove,
              onInsert: insert,
              keepFocus,
              notice,
            })
          : null,
      );
    }

    const en = {
      open: 'Prompt palette',
      title: 'Prompt palette',
      hint: 'Click to insert at the caret; nothing is sent automatically.',
      favorites: 'Favourites',
      recent: 'Recently sent',
      addPlaceholder: 'Add a favourite prompt, then press Enter',
      add: 'Add',
      remove: 'Remove',
      emptyCustom: 'Add your own prompts below.',
      insertFailed: 'Insert failed: the draft changed. Try again.',
      'prompt.continue': 'Continue with the next step',
      'prompt.explain': 'Explain this part in detail',
      'prompt.plan': 'Do not edit yet; lay out the plan first',
      'prompt.test': 'Run the tests and fix any failures',
      'prompt.summary': 'Summarize the changes just made',
      'prompt.memory': 'Update MEMORY.MD for this stage',
    };

    const zh = {
      open: '提示词便签',
      title: '提示词便签',
      hint: '点击后插入到光标处，不会自动发送。',
      favorites: '常用',
      recent: '最近发送',
      addPlaceholder: '添加一条常用提示，回车保存',
      add: '添加',
      remove: '删除',
      emptyCustom: '还没有自定义提示，可在下方添加。',
      insertFailed: '插入失败：输入框内容刚刚变化，请重试。',
      'prompt.continue': '继续下一步',
      'prompt.explain': '详细解释这一部分',
      'prompt.plan': '先别改代码，先讲清方案',
      'prompt.test': '运行测试并修复失败',
      'prompt.summary': '总结这次改动',
      'prompt.memory': '更新 MEMORY.MD 的这个阶段',
    };

    return {
      inject: ['slots', 'locale'],
      apply(ctx) {
        ctx.effect(installStyle, 'prompt-palette: styles');
        ctx.effect(() => ctx.locale.register(NS, { zh, en }), 'prompt-palette: dictionaries');
        ctx.slots.inject('conversation.input.left', () =>
          ctx.slots.register(
            {
              name: 'conversation.input.left',
              id: 'prompt-palette',
              order: 30,
              locale: NS,
            },
            PromptPalette,
          ),
        );
      },
    };
  },
});
