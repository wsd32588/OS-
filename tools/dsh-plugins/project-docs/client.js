/**
 * Project docs (Client half).
 *
 * One compact control beside the composer model selector that opens the
 * workspace's working documents in the right sidebar. Paths are workspace
 * relative and resolved through the authenticated Session file address, so
 * the control never reads files itself.
 */
window.__ModuleLoader__.load({
  id: '@local/dsh-project-docs',
  factory(require) {
    const React = require('react');
    const h = React.createElement;
    const { useCallback, useEffect, useRef, useState } = React;

    const NS = 'projectDocs';
    const STYLE_ID = '@local/dsh-project-docs/project-docs.css';

    const DOCS = [
      { path: 'MEMORY.MD', key: 'doc.memory' },
      { path: 'AGENT.MD', key: 'doc.agent' },
      { path: 'AGENTS.md', key: 'doc.agents' },
      { path: 'RISCV_CHEATSHEET.md', key: 'doc.riscv' },
      { path: 'EDITOR_CHEATSHEET.md', key: 'doc.editor' },
      { path: 'Makefile', key: 'doc.makefile' },
    ];

    const CSS = `
.dpd-root { position: relative; display: inline-flex; align-items: center; }
.dpd-trigger {
  height: 28px; display: inline-flex; align-items: center; gap: 4px;
  padding: 0 8px; border: none; border-radius: 6px; cursor: pointer;
  background: transparent; color: var(--dsw-alias-label-secondary);
  font: inherit; font-size: 12px; line-height: 1;
}
.dpd-trigger:hover { background: var(--dsw-alias-bg-layer-2); color: var(--dsw-alias-label-primary); }
.dpd-trigger:focus-visible, .dpd-item:focus-visible {
  outline: 2px solid var(--dsw-alias-brand-primary); outline-offset: 1px;
}
.dpd-trigger svg { width: 14px; height: 14px; display: block; }
.dpd-panel {
  position: absolute; right: 0; bottom: calc(100% + 10px); z-index: 40;
  box-sizing: border-box; width: 264px; max-width: calc(100vw - 48px);
  max-height: min(60vh, 420px); overflow-y: auto;
  display: flex; flex-direction: column; gap: 1px;
  padding: 8px; border-radius: 10px;
  background: var(--dsw-alias-bg-overlay);
  border: 1px solid var(--dsw-alias-border-l1);
  color: var(--dsw-alias-label-primary);
  font-size: 12px; line-height: 18px;
}
.dpd-head { padding: 2px 8px 6px; }
.dpd-title { font-size: 12px; font-weight: 600; }
.dpd-hint { color: var(--dsw-alias-label-secondary); font-size: 11px; line-height: 16px; }
.dpd-item {
  display: flex; align-items: baseline; justify-content: space-between; gap: 8px;
  padding: 5px 8px; border: none; border-radius: 6px; cursor: pointer;
  background: transparent; color: var(--dsw-alias-label-primary);
  font: inherit; text-align: left;
}
.dpd-item:hover { background: var(--dsw-alias-bg-layer-2); }
.dpd-itemPath { color: var(--dsw-alias-label-secondary); font-size: 10px; white-space: nowrap; }
.dpd-notice { padding: 4px 8px; color: var(--dsw-alias-state-warn-primary); font-size: 11px; line-height: 16px; }
`;

    function installStyle() {
      if (typeof document === 'undefined') return () => {};
      const existing = document.querySelector('style[data-plugin-css=' + JSON.stringify(STYLE_ID) + ']');
      if (existing !== null) return () => {};
      const tag = document.createElement('style');
      tag.dataset.plugin = '@local/dsh-project-docs';
      tag.dataset.pluginCss = STYLE_ID;
      tag.textContent = CSS;
      document.head.appendChild(tag);
      return () => tag.remove();
    }

    /** Encode one id or path segment, keeping `:` literal for drive letters. */
    function encodeSegment(segment) {
      return encodeURIComponent(segment).replace(/%3A/gi, ':');
    }

    /** Build the authenticated Session file address for one workspace path. */
    function fileAddress(sessionId, path) {
      const normalized = path.replace(/\\/g, '/').replace(/^(?:\.\/)+/, '');
      return 'dsh-resource://file/session/' + encodeSegment(sessionId) + '/' + normalized.split('/').map(encodeSegment).join('/');
    }

    function ProjectDocs({ openDoc, t }) {
      const [open, setOpen] = useState(false);
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

      const choose = useCallback(
        (path) => {
          try {
            if (openDoc(path)) {
              setOpen(false);
              setNotice(null);
            } else {
              setNotice(t('unavailable'));
            }
          } catch {
            setNotice(t('unavailable'));
          }
        },
        [openDoc, t],
      );

      const trigger = h(
        'button',
        {
          type: 'button',
          className: 'dpd-trigger',
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
          { viewBox: '0 0 16 16', 'aria-hidden': true },
          h('path', {
            d: 'M3 2.5h6.5L13 6v7.5H3z',
            fill: 'none',
            stroke: 'currentColor',
            'stroke-width': 1.3,
            'stroke-linejoin': 'round',
          }),
          h('path', { d: 'M9.5 2.5V6H13', fill: 'none', stroke: 'currentColor', 'stroke-width': 1.3, 'stroke-linejoin': 'round' }),
        ),
        t('label'),
      );

      return h(
        'div',
        { className: 'dpd-root', ref: rootRef },
        trigger,
        open
          ? h(
              'div',
              { className: 'dpd-panel', role: 'dialog', 'aria-label': t('open'), 'aria-modal': 'false' },
              h(
                'div',
                { className: 'dpd-head' },
                h('div', { className: 'dpd-title' }, t('title')),
                h('div', { className: 'dpd-hint' }, t('hint')),
              ),
              DOCS.map((doc) =>
                h(
                  'button',
                  {
                    type: 'button',
                    key: doc.path,
                    className: 'dpd-item',
                    title: doc.path,
                    onMouseDown: keepFocus,
                    onClick: () => choose(doc.path),
                  },
                  h('span', null, t(doc.key)),
                  h('span', { className: 'dpd-itemPath' }, doc.path),
                ),
              ),
              notice === null ? null : h('div', { className: 'dpd-notice' }, notice),
            )
          : null,
      );
    }

    const en = {
      open: 'Open project documents',
      label: 'Docs',
      title: 'Project documents',
      hint: 'Opens the file in the right sidebar.',
      unavailable: 'The right sidebar is not available for this Session.',
      'doc.memory': 'Memory',
      'doc.agent': 'Rules',
      'doc.agents': 'Entry point',
      'doc.riscv': 'RISC-V sheet',
      'doc.editor': 'Editor sheet',
      'doc.makefile': 'Build targets',
    };

    const zh = {
      open: '打开项目文档',
      label: '速查',
      title: '项目文档',
      hint: '在右侧栏打开对应文件。',
      unavailable: '当前会话的右侧栏不可用。',
      'doc.memory': '项目记忆',
      'doc.agent': '协作约定',
      'doc.agents': '入口说明',
      'doc.riscv': 'RISC-V 速查',
      'doc.editor': '编辑器速查',
      'doc.makefile': '构建目标',
    };

    return {
      inject: ['slots', 'locale'],
      apply(ctx) {
        ctx.effect(installStyle, 'project-docs: styles');
        ctx.effect(() => ctx.locale.register(NS, { zh, en }), 'project-docs: dictionaries');
        ctx.slots.inject('conversation.input.right', () =>
          ctx.slots.register(
            {
              name: 'conversation.input.right',
              id: 'project-docs',
              order: 5,
              locale: NS,
              inject: (sessionId) => ({
                openDoc: (path) => {
                  const controller = ctx.get('sidebarRight');
                  if (controller === undefined || controller === null || typeof controller.openResource !== 'function') return false;
                  controller.openResource(fileAddress(sessionId, path));
                  return true;
                },
              }),
            },
            ProjectDocs,
          ),
        );
      },
    };
  },
});
