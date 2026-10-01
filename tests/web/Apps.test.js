import { beforeEach, describe, expect, it, vi } from 'vitest'

vi.mock('../../src_assets/common/assets/web/Navbar.vue', () => ({
  default: { template: '<div />' },
}))
vi.mock('../../src_assets/common/assets/web/fetch_utils', () => ({
  apiFetch: vi.fn(() => Promise.resolve({ status: 200 })),
}))

import { apiFetch } from '../../src_assets/common/assets/web/fetch_utils'
import Apps from '../../src_assets/common/assets/web/Apps.vue'

beforeEach(() => vi.clearAllMocks())

/**
 * Build a minimal context object that satisfies
 * the `this` contract of fileBrowserConfirm().
 *
 * @param {string} path Selected file path.
 * @param {string[]|null} acceptedExtensions Allowed extensions or null.
 * @param {string} type Browser type (e.g. 'file', 'directory').
 * @returns {object} A stub context for `.call()`.
 */
function browserContext(path, acceptedExtensions, type = 'file') {
  return {
    fileBrowserSelectedPath: path,
    fileBrowserTypedPath: '',
    fileBrowserAcceptedExtensions: acceptedExtensions,
    fileBrowserType: type,
    fileBrowserError: '',
    fileBrowserCallback: vi.fn(),
    fileBrowserClose: vi.fn(),
    $t: (key, _params) => key,
  }
}

/**
 * Build a minimal context object that satisfies
 * the `this` contract of save().
 *
 * @param {string} imagePath The image-path value in the edit form.
 * @returns {object} A stub context for `.call()`.
 */
function saveContext(imagePath) {
  const modalBody = { scrollTop: 100 }
  return {
    editForm: { 'image-path': imagePath },
    editFormError: '',
    $refs: { editModal: { querySelector: () => modalBody } },
    $t: (key, _params) => key,
    _modalBody: modalBody,
  }
}

describe('fileBrowserConfirm – extension validation', () => {
  it('rejects a non-PNG file when acceptedExtensions is [".png"]', () => {
    const ctx = browserContext('/covers/cover.jpg', ['.png'])
    const onSelect = ctx.fileBrowserCallback

    Apps.methods.fileBrowserConfirm.call(ctx)

    expect(ctx.fileBrowserError).toBe('file_browser.error_invalid_extension')
    expect(onSelect).not.toHaveBeenCalled()
    expect(ctx.fileBrowserClose).not.toHaveBeenCalled()
  })

  it('accepts a PNG with mixed-case extension', () => {
    const ctx = browserContext('/covers/cover.PNG', ['.png'])
    const onSelect = ctx.fileBrowserCallback

    Apps.methods.fileBrowserConfirm.call(ctx)

    expect(ctx.fileBrowserError).toBe('')
    expect(onSelect).toHaveBeenCalledWith('/covers/cover.PNG')
    expect(ctx.fileBrowserClose).toHaveBeenCalledOnce()
  })

  it('allows any extension when acceptedExtensions is null', () => {
    const ctx = browserContext('/output/log.txt', null)
    const onSelect = ctx.fileBrowserCallback

    Apps.methods.fileBrowserConfirm.call(ctx)

    expect(ctx.fileBrowserError).toBe('')
    expect(onSelect).toHaveBeenCalledWith('/output/log.txt')
    expect(ctx.fileBrowserClose).toHaveBeenCalledOnce()
  })

  it('skips extension check for directory type even with acceptedExtensions', () => {
    const ctx = browserContext('/some/directory', ['.png'], 'directory')
    const onSelect = ctx.fileBrowserCallback

    Apps.methods.fileBrowserConfirm.call(ctx)

    expect(ctx.fileBrowserError).toBe('')
    expect(onSelect).toHaveBeenCalledWith('/some/directory')
    expect(ctx.fileBrowserClose).toHaveBeenCalledOnce()
  })

  it('does nothing when no path is selected', () => {
    const ctx = browserContext('', ['.png'])
    const onSelect = ctx.fileBrowserCallback

    Apps.methods.fileBrowserConfirm.call(ctx)

    expect(ctx.fileBrowserError).toBe('')
    expect(onSelect).not.toHaveBeenCalled()
    expect(ctx.fileBrowserClose).not.toHaveBeenCalled()
  })

  it('uses fileBrowserTypedPath as fallback when selectedPath is empty', () => {
    const ctx = browserContext('', ['.png'])
    ctx.fileBrowserTypedPath = '/covers/art.png'
    const onSelect = ctx.fileBrowserCallback

    Apps.methods.fileBrowserConfirm.call(ctx)

    expect(onSelect).toHaveBeenCalledWith('/covers/art.png')
    expect(ctx.fileBrowserClose).toHaveBeenCalledOnce()
  })
})

describe('save – cover image validation', () => {
  it('blocks a manually typed non-PNG image path', () => {
    const ctx = saveContext('/covers/cover.jpg')

    Apps.methods.save.call(ctx)

    expect(ctx.editFormError).toBe('file_browser.error_invalid_extension')
    expect(ctx._modalBody.scrollTop).toBe(0)
    expect(apiFetch).not.toHaveBeenCalled()
  })

  it('blocks a .bmp image path', () => {
    const ctx = saveContext('/covers/image.bmp')

    Apps.methods.save.call(ctx)

    expect(ctx.editFormError).toBe('file_browser.error_invalid_extension')
    expect(apiFetch).not.toHaveBeenCalled()
  })

  it('accepts a valid .PNG path (mixed case) and submits', () => {
    const ctx = saveContext('/covers/cover.PnG')

    Apps.methods.save.call(ctx)

    expect(ctx.editFormError).toBe('')
    expect(apiFetch).toHaveBeenCalledOnce()
    expect(apiFetch).toHaveBeenCalledWith(
      './api/apps',
      expect.objectContaining({ method: 'POST' })
    )
  })

  it('submits when the image path is empty', () => {
    const ctx = saveContext('')

    Apps.methods.save.call(ctx)

    expect(ctx.editFormError).toBe('')
    expect(apiFetch).toHaveBeenCalledOnce()
  })

  it('strips double quotes from the image path before validating', () => {
    const ctx = saveContext('""/covers/cover.png""')

    Apps.methods.save.call(ctx)

    expect(ctx.editForm['image-path']).toBe('/covers/cover.png')
    expect(ctx.editFormError).toBe('')
    expect(apiFetch).toHaveBeenCalledOnce()
  })
})
