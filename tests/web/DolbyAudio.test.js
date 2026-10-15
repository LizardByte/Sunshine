import { flushPromises, mount } from '@vue/test-utils'
import { afterEach, describe, expect, it, vi } from 'vitest'
import Config from '../../src_assets/common/assets/web/Config.vue'

const globalOptions = {
  mocks: { $t: key => key },
  stubs: {
    Navbar: true, General: true, Inputs: true, Network: true, Files: true,
    Advanced: true, ContainerEncoders: true,
    PlatformLayout: { template: '<div><slot /><slot name="windows" /></div>' },
    AdapterNameSelector: true, DisplayOutputSelector: true,
    DisplayDeviceOptions: true, DisplayModesSettings: true,
  },
}
let wrapper
afterEach(() => { wrapper?.unmount(); wrapper = undefined; vi.unstubAllGlobals() })
async function mountConfig({ ac3Available, eac3Available, audioFormat }) {
  vi.stubGlobal('fetch', vi.fn().mockResolvedValue({
    json: () => Promise.resolve({
      status: true, platform: 'windows', version: 'test',
      ac3_available: ac3Available, eac3_available: eac3Available,
      audio_format: audioFormat, high_quality_audio: false,
    }),
  }))
  wrapper = mount(Config, { global: globalOptions })
  await flushPromises()
  wrapper.vm.currentTab = 'av'
  await wrapper.vm.$nextTick()
  return wrapper
}
describe('Dolby audio configuration capabilities', () => {
  it('preserves a saved Dolby format when the API reports the encoder available', async () => {
    const page = await mountConfig({ ac3Available: true, eac3Available: true, audioFormat: 'eac3' })
    const format = page.get('#audio_format')
    expect(format.element.value).toBe('eac3')
    expect(format.element.disabled).toBe(false)
    expect(page.get('#audio_format option[value="ac3"]').element.disabled).toBe(false)
    expect(page.get('#audio_format option[value="eac3"]').element.disabled).toBe(false)
    expect(page.get('#high_quality_audio').element.disabled).toBe(false)
    expect(page.vm.config).not.toHaveProperty('ac3_available')
    expect(page.vm.config).not.toHaveProperty('eac3_available')
  })
  it('disables only the unavailable codec and safely falls back from it', async () => {
    const page = await mountConfig({ ac3Available: true, eac3Available: false, audioFormat: 'eac3' })
    const format = page.get('#audio_format')
    expect(format.element.value).toBe('opus')
    expect(format.element.disabled).toBe(false)
    expect(page.get('#audio_format option[value="ac3"]').element.disabled).toBe(false)
    expect(page.get('#audio_format option[value="eac3"]').element.disabled).toBe(true)
    expect(page.get('#high_quality_audio').element.disabled).toBe(true)
  })
  it('disables Dolby selection when neither encoder is available', async () => {
    const page = await mountConfig({ ac3Available: false, eac3Available: false, audioFormat: 'ac3' })
    const format = page.get('#audio_format')
    expect(format.element.value).toBe('opus')
    expect(format.element.disabled).toBe(true)
    expect(page.get('#audio_format option[value="ac3"]').element.disabled).toBe(true)
    expect(page.get('#audio_format option[value="eac3"]').element.disabled).toBe(true)
    expect(page.find('#high_quality_audio').exists()).toBe(false)
  })
})
