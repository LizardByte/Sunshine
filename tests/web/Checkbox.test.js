import { mount } from '@vue/test-utils'
import { describe, expect, it } from 'vitest'

import Checkbox from '../../src_assets/common/assets/web/Checkbox.vue'

const translations = {
  '_common.disabled_def_cbox': 'Disabled by default',
  '_common.enabled_def_cbox': 'Enabled by default',
}

function translate(key) {
  return translations[key] ?? key
}

function mountCheckbox(props = {}) {
  return mount(Checkbox, {
    props: {
      id: 'test-option',
      modelValue: false,
      ...props,
    },
    global: {
      mocks: {
        $t: translate,
      },
    },
  })
}

describe('Checkbox', () => {
  it('associates its translated label and description with the input', () => {
    const wrapper = mountCheckbox({
      default: true,
      localePrefix: 'config',
    })

    expect(wrapper.get('label').attributes('for')).toBe('test-option')
    expect(wrapper.get('label').text()).toContain('config.test-option')
    expect(wrapper.get('label').text()).toContain('Enabled by default')
    expect(wrapper.findAll('.form-text')[1].text()).toBe('config.test-option_desc')
  })

  it('preserves string values when the checkbox changes', async () => {
    const wrapper = mountCheckbox({ modelValue: 'false' })

    await wrapper.get('input').setValue(true)

    expect(wrapper.emitted('update:modelValue')).toEqual([['true']])
  })

  it('supports inverted values without changing their type', async () => {
    const wrapper = mountCheckbox({
      inverseValues: true,
      modelValue: 1,
    })

    await wrapper.get('input').setValue(true)

    expect(wrapper.emitted('update:modelValue')).toEqual([[0]])
  })
})
