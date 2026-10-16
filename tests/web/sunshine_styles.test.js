import { mount, flushPromises } from '@vue/test-utils'
import { parse } from '@vue/compiler-sfc'
import { readFileSync } from 'node:fs'
import { dirname, resolve } from 'node:path'
import { fileURLToPath } from 'node:url'
import { afterAll, afterEach, beforeAll, describe, expect, it, vi } from 'vitest'
import navbarSource from '../../src_assets/common/assets/web/Navbar.vue?raw'

vi.mock('../../src_assets/common/assets/web/Navbar.vue', () => ({
  default: { template: '<div />' },
}))

import Featured from '../../src_assets/common/assets/web/Featured.vue'

const navbarCss = parse(navbarSource).descriptor.styles[0].content
const projectRoot = resolve(dirname(fileURLToPath(import.meta.url)), '../..')
const bootstrapCss = readFileSync(resolve(projectRoot, 'node_modules/bootstrap/dist/css/bootstrap.css'), 'utf8')
const sunshineCss = readFileSync(resolve(projectRoot, 'src_assets/common/assets/web/sunshine.css'), 'utf8')
let stylesheet
let wrapper

beforeAll(() => {
  stylesheet = document.createElement('style')
  stylesheet.textContent = [bootstrapCss, sunshineCss, navbarCss].join('\n')
  document.head.append(stylesheet)
})

afterEach(() => {
  wrapper?.unmount()
  wrapper = undefined
  document.body.replaceChildren()
  delete document.documentElement.dataset.theme
  delete document.documentElement.dataset.bsTheme
  vi.unstubAllGlobals()
})

afterAll(() => {
  stylesheet.remove()
})

/**
 * @brief Read the winning style with Bootstrap and the Sunshine theme loaded.
 *
 * @param {string} markup Component markup to insert into the document.
 * @param {string} selector Element whose computed styles should be returned.
 * @returns {CSSStyleDeclaration} Computed style, including unresolved theme variables in jsdom.
 */
function componentStyle(markup, selector) {
  document.body.innerHTML = markup
  return getComputedStyle(document.querySelector(selector))
}

/**
 * @brief Mount the featured page with a deterministic app directory.
 *
 * @param {object} directory App and category data returned by the directory service.
 * @returns {Promise<import('@vue/test-utils').VueWrapper>} Mounted featured page.
 */
async function mountFeatured(directory) {
  vi.stubGlobal('fetch', vi.fn(async () => ({
    ok: true,
    json: async () => directory,
  })))
  wrapper = mount(Featured, {
    attachTo: document.body,
    global: { mocks: { $t: key => key }, stubs: { Navbar: true, SimpleIcon: true } },
  })
  await flushPromises()
  return wrapper
}

const componentCases = [
  ['body', '<div></div>', 'body', { backgroundColor: 'var(--color-bg-base)', color: 'var(--color-text-base)' }],
  ['card', '<div class="card"></div>', '.card', { backgroundColor: 'var(--color-surface)', color: 'var(--color-text-base)' }],
  ['card header', '<div class="card-header"></div>', '.card-header', { backgroundColor: 'var(--color-bg-subtle)', color: 'var(--color-text-base)' }],
  ['card footer', '<div class="card-footer"></div>', '.card-footer', { backgroundColor: 'var(--color-bg-subtle)' }],
  ['modal', '<div class="modal-content"></div>', '.modal-content', { backgroundColor: 'var(--color-surface)' }],
  ['modal header', '<div class="modal-header"></div>', '.modal-header', { backgroundColor: 'var(--color-surface-raised)' }],
  ['modal footer', '<div class="modal-footer"></div>', '.modal-footer', { backgroundColor: 'var(--color-surface-raised)' }],
  ['list item', '<div class="list-group-item"></div>', '.list-group-item', { backgroundColor: 'var(--color-surface)', color: 'var(--color-text-base)' }],
  ['accordion', '<div class="accordion-item"></div>', '.accordion-item', { backgroundColor: 'var(--color-surface)' }],
  ['collapsed accordion button', '<button class="accordion-button collapsed"></button>', 'button', { backgroundColor: 'var(--color-bg-subtle)', color: 'var(--color-text-base)' }],
  ['expanded accordion button', '<button class="accordion-button"></button>', 'button', { backgroundColor: 'var(--color-bg-muted)', color: 'var(--color-text-base)' }],
  ['offcanvas', '<div class="offcanvas"></div>', '.offcanvas', { backgroundColor: 'var(--color-surface)' }],
  ['offcanvas header', '<div class="offcanvas-header"></div>', '.offcanvas-header', { color: 'var(--color-text-base)' }],
  ['offcanvas body', '<div class="offcanvas-body"></div>', '.offcanvas-body', { color: 'var(--color-text-base)' }],
  ['toast', '<div class="toast"></div>', '.toast', { backgroundColor: 'var(--color-surface)' }],
  ['toast header', '<div class="toast-header"></div>', '.toast-header', { backgroundColor: 'var(--color-bg-subtle)', color: 'var(--color-text-base)' }],
  ['toast body', '<div class="toast-body"></div>', '.toast-body', { color: 'var(--color-text-base)' }],
  ['popover', '<div class="popover"></div>', '.popover', { backgroundColor: 'var(--color-surface)' }],
  ['popover header', '<div class="popover-header"></div>', '.popover-header', { backgroundColor: 'var(--color-bg-subtle)', color: 'var(--color-text-base)' }],
  ['popover body', '<div class="popover-body"></div>', '.popover-body', { color: 'var(--color-text-base)' }],
  ['tooltip', '<div class="tooltip-inner"></div>', '.tooltip-inner', { backgroundColor: 'var(--color-bg-muted)', color: 'var(--color-text-base)' }],
  ['breadcrumb', '<ol class="breadcrumb"></ol>', '.breadcrumb', { backgroundColor: 'var(--color-bg-subtle)' }],
  ['breadcrumb link', '<ol class="breadcrumb"><li class="breadcrumb-item"><a href="#">Page</a></li></ol>', 'a', { color: 'var(--color-primary)' }],
  ['active breadcrumb', '<ol class="breadcrumb"><li class="breadcrumb-item active">Page</li></ol>', '.breadcrumb-item', { color: 'var(--color-text-muted)' }],
  ['page link', '<ul class="pagination"><li class="page-item"><a class="page-link">1</a></li></ul>', '.page-link', { color: 'var(--color-text-base)' }],
  ['navbar link', '<nav class="navbar navbar-expand-lg navbar-sunshine"><ul class="navbar-nav"><li><a class="nav-link">Page</a></li></ul></nav>', '.nav-link', { color: 'var(--navbar-text-muted)', paddingTop: '8px', paddingRight: '16px', paddingBottom: '8px', paddingLeft: '16px' }],
  ['simple navbar theme control', '<nav class="navbar navbar-sunshine"><button class="nav-link">Theme</button></nav>', 'button', { color: 'var(--navbar-text-muted)', paddingTop: '8px', paddingRight: '16px', paddingBottom: '8px', paddingLeft: '16px' }],
  ['active navbar link', '<nav class="navbar-sunshine"><ul class="navbar-nav"><li><a class="nav-link active">Page</a></li></ul></nav>', '.nav-link', { color: 'var(--navbar-text)' }],
  ['open navbar dropdown', '<nav class="navbar-sunshine"><ul class="navbar-nav"><li><button class="nav-link dropdown-toggle show">Menu</button></li></ul></nav>', 'button', { color: 'var(--navbar-text)' }],
  ['navbar toggler', '<nav class="navbar-sunshine"><button class="navbar-toggler"></button></nav>', 'button', { color: 'var(--navbar-text)', borderTopColor: 'rgba(255, 255, 255, 0.2)' }],
  ['disabled button', '<button class="btn btn-primary" disabled>Save</button>', 'button', { transform: 'none' }],
  ['busy cover', '<div class="cover-finder"><div class="cover-results busy"><button class="cover-container result"></button></div></div>', 'button', { cursor: 'wait', pointerEvents: 'none' }],
  ['release notes', '<div class="markdown-body release-notes"></div>', '.release-notes', { columnCount: '1', display: 'block' }],
  ['dropdown', '<div class="dropdown-menu"></div>', '.dropdown-menu', { backgroundColor: 'var(--color-surface)' }],
  ['dropdown item', '<div class="dropdown-menu"><button class="dropdown-item">Theme</button></div>', 'button', { color: 'var(--color-text-base)' }],
  ['active dropdown item', '<div class="dropdown-menu"><button class="dropdown-item active">Theme</button></div>', 'button', { color: 'var(--color-text-base)' }],
  ['dropdown divider', '<div class="dropdown-menu"><hr class="dropdown-divider"></div>', 'hr', { opacity: '1' }],
  ['featured icon', '<div class="featured-app-icon"><img alt="App"></div>', 'img', { maxWidth: '64px', maxHeight: '64px' }],
  ['screenshot', '<button class="screenshot-thumbnail-button"><img class="screenshot-thumbnail" alt="Screenshot"></button>', 'img', { height: '120px', maxWidth: '200px' }],
]

describe.each(['light', 'dark'])('%s theme cascade', theme => {
  it.each([0, 1, 2])('rounds the outside category filter corners with %i categories', async categoryCount => {
    document.documentElement.dataset.theme = theme
    document.documentElement.dataset.bsTheme = theme
    const categories = [
      { id: 'clients', originalId: 'clients' },
      { id: 'tools', originalId: 'tools' },
    ].slice(0, categoryCount)
    await mountFeatured({ apps: [], categories })

    expect(wrapper.get('fieldset legend').text()).toBe('featured.category_filter')
    const buttons = wrapper.findAll('.btn-group > .btn')
    expect(buttons).toHaveLength(categoryCount + 1)
    const firstStyle = getComputedStyle(buttons[0].element)
    const lastStyle = getComputedStyle(buttons.at(-1).element)
    expect(firstStyle.borderTopLeftRadius).not.toBe('0px')
    expect(firstStyle.borderBottomLeftRadius).not.toBe('0px')
    expect(lastStyle.borderTopRightRadius).not.toBe('0px')
    expect(lastStyle.borderBottomRightRadius).not.toBe('0px')
    if (categoryCount > 0) {
      expect(firstStyle.borderTopRightRadius).toBe('0px')
      expect(firstStyle.borderBottomRightRadius).toBe('0px')
      expect(lastStyle.borderTopLeftRadius).toBe('0px')
      expect(lastStyle.borderBottomLeftRadius).toBe('0px')
      for (const button of buttons.slice(1, -1)) {
        const style = getComputedStyle(button.element)
        expect([style.borderTopLeftRadius, style.borderBottomLeftRadius, style.borderTopRightRadius, style.borderBottomRightRadius]).toEqual(['0px', '0px', '0px', '0px'])
      }
      await buttons[1].trigger('click')
      expect(buttons[1].classes()).toContain('active')
      await buttons[0].trigger('click')
      expect(buttons[0].classes()).toContain('active')
    }
  })

  it('allows specific component rules to override theme defaults', () => {
    document.documentElement.dataset.theme = theme
    document.documentElement.dataset.bsTheme = theme
    const override = document.createElement('style')
    override.textContent = '.card.custom-card { background-color: rgb(1, 2, 3); color: rgb(4, 5, 6); }'
    document.head.append(override)
    try {
      const style = componentStyle('<div class="card custom-card"></div>', '.card')
      expect(style.backgroundColor).toBe('rgb(1, 2, 3)')
      expect(style.color).toBe('rgb(4, 5, 6)')
    } finally {
      override.remove()
    }
  })

  it.each(componentCases)('preserves %s styles', (_name, markup, selector, expected) => {
    document.documentElement.dataset.theme = theme
    document.documentElement.dataset.bsTheme = theme
    const style = componentStyle(markup, selector)
    for (const [property, value] of Object.entries(expected)) {
      expect(style[property], property).toBe(value)
    }
  })

  it('themes the rendered official and platform badges without Bootstrap background utilities', async () => {
    document.documentElement.dataset.theme = theme
    document.documentElement.dataset.bsTheme = theme
    await mountFeatured({ apps: [{ id: 'sample', name: 'Sample App', official: true, platforms: ['linux'] }] })

    const [official, platform] = wrapper.findAll('.badge')
    expect(getComputedStyle(official.element).backgroundColor).toBe('var(--color-primary)')
    expect(getComputedStyle(official.element).color).toBe('var(--color-on-primary)')
    expect(getComputedStyle(platform.element).backgroundColor).toBe('var(--color-bg-muted)')
    expect(getComputedStyle(platform.element).color).toBe('var(--color-text-base)')
  })
})
