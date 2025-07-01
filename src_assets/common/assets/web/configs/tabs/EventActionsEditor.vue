<template>
  <div class="event-actions-editor" data-testid="event-actions-editor">
    <!-- Custom Header Slot -->
    <slot name="header">
      <!-- Default header -->
      <label class="form-label">Event Actions</label>
    </slot>

    <!-- Custom Description Slot and Add Action Button Row -->
    <div class="row mb-4">
      <div class="col-12 d-flex align-items-start" style="min-height: 0">
        <div class="flex-grow-1 col-md-9 pe-3 d-flex align-items-center" style="min-height: 0">
          <slot name="description">
            <!-- Default Event Actions Overview -->
            <div
              class="alert alert-primary mb-0 shadow-sm border-0"
              style="background: linear-gradient(135deg, #e3f2fd 0%, #f3e5f5 100%)"
            >
              <div class="d-flex align-items-start">
                <div class="me-3">
                  <i class="fas fa-bolt text-primary" style="font-size: 1.5rem" />
                </div>
                <div>
                  <h6 class="alert-heading mb-2 text-primary">Event Actions</h6>
                  <p class="mb-0 text-dark">
                    Automate tasks at key moments in your app or stream lifecycle. Run scripts
                    before a stream starts, after it ends, or when a client connects. Use this
                    editor to add, edit, or remove actions for each stage.
                  </p>
                </div>
              </div>
            </div>
          </slot>
        </div>
        <div class="col-md-3 d-flex justify-content-end align-items-start" style="min-height: 0">
          <button
            class="btn btn-primary shadow-sm px-4 py-2 fw-semibold"
            style="
              border-radius: 10px;
              background: linear-gradient(135deg, #0d6efd 0%, #6610f2 100%);
              border: none;
            "
            @click="showNewAction"
          >
            <i class="fas fa-plus me-2" /> {{ $t('event_actions.new_action') }}
          </button>
        </div>
      </div>
    </div>

    <div>
      <template v-if="eventActions.length > 0">
        <div class="actions-grid">
          <div
            v-for="(action, actionIdx) in eventActions"
            :key="actionIdx"
            class="card mb-4 shadow-sm border-0"
            style="border-radius: 12px; overflow: hidden"
          >
            <div
              class="card-header bg-white border-0 py-3 px-4"
              style="background: linear-gradient(135deg, #f8f9fa 0%, #ffffff 100%)"
            >
              <div class="d-flex justify-content-between align-items-center">
                <div class="d-flex align-items-center">
                  <div class="action-icon me-3">
                    <i class="fas fa-cog text-primary" style="font-size: 1.2rem" />
                  </div>
                  <div>
                    <h6 class="mb-1 fw-bold text-dark">
                      {{ action.name }}
                    </h6>
                    <small class="text-muted d-flex align-items-center">
                      <i class="fas fa-play-circle me-1 text-success" />
                      {{ getStageDisplayName(action.action.startup_stage) }}
                      <span v-if="action.action.shutdown_stage" class="mx-2">
                        <i class="fas fa-arrow-right text-muted" />
                      </span>
                      <span v-if="action.action.shutdown_stage" class="d-flex align-items-center">
                        <i class="fas fa-stop-circle me-1 text-warning" />
                        {{ getStageDisplayName(action.action.shutdown_stage) }}
                      </span>
                    </small>
                  </div>
                </div>
                <div class="d-flex gap-2">
                  <button
                    class="btn btn-sm btn-outline-primary border-0 shadow-sm fw-medium"
                    style="border-radius: 8px; padding: 8px 16px"
                    @click="editAction(actionIdx)"
                  >
                    <i class="fas fa-edit me-1" /> Edit
                  </button>
                  <button
                    class="btn btn-sm btn-outline-danger border-0 shadow-sm fw-medium"
                    style="border-radius: 8px; padding: 8px 16px"
                    @click="deleteAction(actionIdx)"
                  >
                    <i class="fas fa-trash me-1" /> Delete
                  </button>
                </div>
              </div>
            </div>
            <div class="card-body p-0">
              <div
                class="action-columns d-flex flex-row scrollable-commands"
                style="min-height: 200px"
              >
                <!-- Startup Commands Column (per-command placeholder logic) -->
                <div
                  class="action-col flex-fill border-end p-4 d-flex flex-column action-col-startup"
                >
                  <div class="d-flex align-items-center justify-content-between mb-3">
                    <h6 class="text-success mb-0 d-flex align-items-center fw-bold">
                      <div class="icon-wrapper me-2 p-2 rounded-circle bg-success bg-opacity-10">
                        <i class="fas fa-play-circle text-success" />
                      </div>
                      Startup Commands
                    </h6>
                    <span
                      class="badge rounded-pill px-3 py-2"
                      :class="
                        action.action.startup_commands &&
                        action.action.startup_commands.failure_policy === 'FAIL_FAST'
                          ? 'bg-warning text-dark'
                          : 'bg-info text-white'
                      "
                    >
                      {{
                        action.action.startup_commands &&
                        action.action.startup_commands.failure_policy === 'FAIL_FAST'
                          ? 'Stop on Error'
                          : 'Continue on Error'
                      }}
                    </span>
                  </div>
                  <div class="flex-grow-1">
                    <div class="commands-list">
                      <template
                        v-for="index in Math.max(
                          action.action.startup_commands.commands.length,
                          action.action.cleanup_commands.commands.length || 0,
                          1
                        )"
                      >
                        <div
                          v-if="
                            action.action.startup_commands.commands[index - 1] &&
                            action.action.startup_commands.commands[index - 1].cmd &&
                            action.action.startup_commands.commands[index - 1].cmd.trim() !== ''
                          "
                          :key="'startup-' + (index - 1)"
                          class="command-item p-4 border-0 rounded-3 shadow-sm"
                          style="
                            background: linear-gradient(135deg, #ffffff 0%, #f8f9fa 100%);
                            margin-bottom: 1rem;
                          "
                        >
                          <div class="d-flex align-items-start flex-nowrap h-100">
                            <div class="command-number me-3 mt-1 flex-shrink-0">
                              <span
                                class="badge bg-success text-white fw-bold shadow-sm"
                                style="
                                  min-width: 28px;
                                  height: 28px;
                                  display: flex;
                                  align-items: center;
                                  justify-content: center;
                                  font-size: 0.75rem;
                                "
                                >{{ index }}</span
                              >
                            </div>
                            <div class="flex-grow-1 min-w-0 d-flex flex-column h-100">
                              <div
                                class="d-flex flex-column flex-grow-1"
                                style="min-height: 5.6em; max-height: 5.6em; height: 5.6em"
                              >
                                <code
                                  class="d-block bg-dark text-light p-3 rounded-3 mb-2 border-0 command-display flex-grow-1"
                                  style="
                                    font-size: 0.9rem;
                                    line-height: 1.4;
                                    min-height: 5.6em;
                                    max-height: 5.6em;
                                    height: 5.6em;
                                    word-break: break-all;
                                    white-space: pre-wrap;
                                    overflow-wrap: anywhere;
                                    overflow: auto;
                                  "
                                >
                                  {{
                                    getTruncatedCommand(
                                      action.action.startup_commands.commands[index - 1].cmd
                                    )
                                  }}
                                </code>
                              </div>
                              <div class="d-flex flex-wrap gap-2 align-items-center mt-auto">
                                <span
                                  v-if="action.action.startup_commands.commands[index - 1].elevated"
                                  class="badge bg-warning text-dark rounded-pill"
                                  title="Run as administrator (Windows) or with sudo (Linux)"
                                >
                                  <i class="fas fa-shield-alt me-1" /> Elevated
                                </span>
                                <span
                                  v-if="action.action.startup_commands.commands[index - 1].async"
                                  class="badge bg-primary rounded-pill"
                                  title="Run in background (fire and forget)"
                                >
                                  <i class="fas fa-play me-1" /> Async
                                </span>
                                <span
                                  v-if="
                                    action.action.startup_commands.commands[index - 1].ignore_error
                                  "
                                  class="badge bg-secondary rounded-pill"
                                  title="Ignore errors and continue"
                                >
                                  <i class="fas fa-exclamation-triangle me-1" /> Ignore Errors
                                </span>
                                <small
                                  class="text-muted fw-medium"
                                  :title="'Timeout: how long to wait before stopping the command'"
                                >
                                  <i class="fas fa-clock me-1" />{{
                                    action.action.startup_commands.commands[index - 1]
                                      .timeout_seconds
                                  }}s
                                </small>
                                <button
                                  type="button"
                                  class="btn btn-sm btn-outline-primary border-0 shadow-sm fw-medium ms-auto"
                                  style="border-radius: 8px; padding: 6px 12px"
                                  @click="editStartupCommand(actionIdx, index - 1)"
                                >
                                  <i class="fas fa-edit me-1" /> Edit
                                </button>
                                <button
                                  type="button"
                                  class="btn btn-sm btn-outline-danger border-0 shadow-sm fw-medium ms-2"
                                  style="border-radius: 8px; padding: 6px 12px"
                                  @click="showDeleteConfirmation('startup', index - 1, actionIdx)"
                                >
                                  <i class="fas fa-trash me-1" /> Delete
                                </button>
                              </div>
                            </div>
                          </div>
                        </div>
                        <div
                          v-else
                          :key="'startup-placeholder-' + (index - 1)"
                          class="command-item p-4 border-0 rounded-3 shadow-sm"
                          style="
                            background: linear-gradient(135deg, #ffffff 0%, #f8f9fa 100%);
                            opacity: 0.7;
                            margin-bottom: 1rem;
                          "
                        >
                          <div class="d-flex align-items-start flex-nowrap h-100">
                            <div class="command-number me-3 mt-1 flex-shrink-0">
                              <span
                                class="badge bg-success text-white fw-bold shadow-sm"
                                style="
                                  min-width: 28px;
                                  height: 28px;
                                  display: flex;
                                  align-items: center;
                                  justify-content: center;
                                  font-size: 0.75rem;
                                "
                                >{{ index }}</span
                              >
                            </div>
                            <div class="flex-grow-1 min-w-0 d-flex flex-column h-100">
                              <div
                                class="d-flex flex-column flex-grow-1"
                                style="min-height: 5.6em; max-height: 5.6em; height: 5.6em"
                              >
                                <code
                                  class="d-block bg-dark text-light p-3 rounded-3 mb-2 border-0 command-display flex-grow-1"
                                  style="
                                    font-size: 0.9rem;
                                    line-height: 1.4;
                                    min-height: 5.6em;
                                    max-height: 5.6em;
                                    height: 5.6em;
                                    word-break: break-all;
                                    white-space: pre-wrap;
                                    overflow-wrap: anywhere;
                                    font-style: italic;
                                    opacity: 0.7;
                                    overflow: auto;
                                    display: flex;
                                    align-items: center;
                                    justify-content: center;
                                  "
                                  >No Command Configured</code
                                >
                              </div>
                              <div class="d-flex flex-wrap gap-2 align-items-center mt-auto">
                                <button
                                  type="button"
                                  class="btn btn-sm btn-outline-primary border-0 shadow-sm fw-medium"
                                  style="border-radius: 8px; padding: 6px 12px"
                                  @click="editStartupCommandOrCreate(actionIdx, index - 1)"
                                >
                                  <i class="fas fa-edit me-1" /> Edit
                                </button>
                                <button
                                  v-if="
                                    action.action.cleanup_commands.commands &&
                                    action.action.cleanup_commands.commands.length > 0 &&
                                    (action.action.cleanup_commands.commands.length - (index - 1) - 1) >= 0 &&
                                    action.action.cleanup_commands.commands[action.action.cleanup_commands.commands.length - (index - 1) - 1] &&
                                    action.action.cleanup_commands.commands[action.action.cleanup_commands.commands.length - (index - 1) - 1].cmd &&
                                    action.action.cleanup_commands.commands[action.action.cleanup_commands.commands.length - (index - 1) - 1].cmd.trim() !== ''
                                  "
                                  type="button"
                                  class="btn btn-sm btn-outline-danger border-0 shadow-sm fw-medium ms-2"
                                  style="border-radius: 8px; padding: 6px 12px"
                                  @click="showDeleteConfirmation('startup', index - 1, actionIdx)"
                                >
                                  <i class="fas fa-trash me-1" /> Delete
                                </button>
                              </div>
                            </div>
                          </div>
                        </div>
                      </template>
                    </div>
                  </div>
                </div>
                <!-- Cleanup Commands Column (per-command placeholder logic) -->
                <div class="action-col flex-fill p-4 d-flex flex-column action-col-cleanup">
                  <div class="d-flex align-items-center justify-content-between mb-3">
                    <h6 class="text-warning mb-0 d-flex align-items-center fw-bold">
                      <div class="icon-wrapper me-2 p-2 rounded-circle bg-warning bg-opacity-10">
                        <i class="fas fa-undo text-warning" />
                      </div>
                      Cleanup Commands
                    </h6>
                    <span
                      class="badge rounded-pill px-3 py-2"
                      :class="
                        action.action.cleanup_commands &&
                        action.action.cleanup_commands.failure_policy === 'FAIL_FAST'
                          ? 'bg-warning text-dark'
                          : 'bg-info text-white'
                      "
                    >
                      {{
                        action.action.cleanup_commands &&
                        action.action.cleanup_commands.failure_policy === 'FAIL_FAST'
                          ? 'Stop on Error'
                          : 'Continue on Error'
                      }}
                    </span>
                  </div>
                  <div class="flex-grow-1">
                    <div class="commands-list">
                      <template
                        v-for="(cmd, displayIdx) in [
                          ...(action.action.cleanup_commands.commands || []),
                        ]
                          .slice()
                          .reverse().length ||
                        Math.max(
                          action.action.startup_commands.commands.length,
                          action.action.cleanup_commands.commands.length || 0,
                          1
                        )
                          ? [...(action.action.cleanup_commands.commands || [])].slice().reverse()
                          : Array(
                              Math.max(
                                action.action.startup_commands.commands.length,
                                action.action.cleanup_commands.commands.length || 0,
                                1
                              )
                            ).fill(null)"
                        :key="'cleanup-' + displayIdx"
                      >
                        <div
                          v-if="cmd && cmd.cmd && cmd.cmd.trim() !== ''"
                          class="command-item p-4 border-0 rounded-3 shadow-sm"
                          style="
                            background: linear-gradient(135deg, #ffffff 0%, #f8f9fa 100%);
                            margin-bottom: 1rem;
                          "
                        >
                          <div class="d-flex align-items-start flex-nowrap h-100">
                            <div class="command-number me-3 mt-1 flex-shrink-0">
                              <span
                                class="badge bg-warning text-dark fw-bold shadow-sm"
                                style="
                                  min-width: 28px;
                                  height: 28px;
                                  display: flex;
                                  align-items: center;
                                  justify-content: center;
                                  font-size: 0.75rem;
                                "
                                >{{
                                  action.action.cleanup_commands.commands.length - displayIdx
                                }}</span
                              >
                            </div>
                            <div class="flex-grow-1 min-w-0 d-flex flex-column h-100">
                              <div
                                class="d-flex flex-column flex-grow-1"
                                style="min-height: 5.6em; max-height: 5.6em; height: 5.6em"
                              >
                                <code
                                  class="d-block bg-dark text-light p-3 rounded-3 mb-2 border-0 command-display flex-grow-1"
                                  style="
                                    font-size: 0.9rem;
                                    line-height: 1.4;
                                    min-height: 5.6em;
                                    max-height: 5.6em;
                                    height: 5.6em;
                                    word-break: break-all;
                                    white-space: pre-wrap;
                                    overflow-wrap: anywhere;
                                    overflow: auto;
                                  "
                                  >{{ getTruncatedCommand(cmd.cmd) }}</code
                                >
                              </div>
                              <div class="d-flex flex-wrap gap-2 align-items-center mt-auto">
                                <span
                                  v-if="cmd.elevated"
                                  class="badge bg-warning text-dark rounded-pill"
                                  title="Run as administrator (Windows) or with sudo (Linux)"
                                >
                                  <i class="fas fa-shield-alt me-1" /> Elevated
                                </span>
                                <span
                                  v-if="cmd.async"
                                  class="badge bg-primary rounded-pill"
                                  title="Run in background (fire and forget)"
                                >
                                  <i class="fas fa-play me-1" /> Async
                                </span>
                                <span
                                  v-if="cmd.ignore_error"
                                  class="badge bg-secondary rounded-pill"
                                  title="Ignore errors and continue"
                                >
                                  <i class="fas fa-exclamation-triangle me-1" /> Ignore Errors
                                </span>
                                <small
                                  class="text-muted fw-medium"
                                  :title="'Timeout: how long to wait before stopping the command'"
                                >
                                  <i class="fas fa-clock me-1" />{{ cmd.timeout_seconds }}s
                                </small>
                                <button
                                  type="button"
                                  class="btn btn-sm btn-outline-primary border-0 shadow-sm fw-medium ms-auto"
                                  style="border-radius: 8px; padding: 6px 12px"
                                  @click="editCleanupCommand(actionIdx, displayIdx)"
                                >
                                  <i class="fas fa-edit me-1" /> Edit
                                </button>
                                <button
                                  type="button"
                                  class="btn btn-sm btn-outline-danger border-0 shadow-sm fw-medium ms-2"
                                  style="border-radius: 8px; padding: 6px 12px"
                                  @click="showDeleteConfirmation('cleanup', displayIdx, actionIdx)"
                                >
                                  <i class="fas fa-trash me-1" /> Delete
                                </button>
                              </div>
                            </div>
                          </div>
                        </div>
                        <div
                          v-else
                          class="command-item p-4 border-0 rounded-3 shadow-sm"
                          style="
                            background: linear-gradient(135deg, #ffffff 0%, #f8f9fa 100%);
                            opacity: 0.7;
                            margin-bottom: 1rem;
                          "
                        >
                          <div class="d-flex align-items-start flex-nowrap h-100">
                            <div class="command-number me-3 mt-1 flex-shrink-0">
                              <span
                                class="badge bg-warning text-dark fw-bold shadow-sm"
                                style="
                                  min-width: 28px;
                                  height: 28px;
                                  display: flex;
                                  align-items: center;
                                  justify-content: center;
                                  font-size: 0.75rem;
                                "
                                >{{
                                  action.action.cleanup_commands.commands.length - displayIdx
                                }}</span
                              >
                            </div>
                            <div class="flex-grow-1 min-w-0 d-flex flex-column h-100">
                              <div
                                class="d-flex flex-column flex-grow-1"
                                style="min-height: 5.6em; max-height: 5.6em; height: 5.6em"
                              >
                                <code
                                  class="d-block bg-dark text-light p-3 rounded-3 mb-2 border-0 command-display flex-grow-1"
                                  style="
                                    font-size: 0.9rem;
                                    line-height: 1.4;
                                    min-height: 5.6em;
                                    max-height: 5.6em;
                                    height: 5.6em;
                                    word-break: break-all;
                                    white-space: pre-wrap;
                                    overflow-wrap: anywhere;
                                    font-style: italic;
                                    opacity: 0.7;
                                    overflow: auto;
                                    display: flex;
                                    align-items: center;
                                    justify-content: center;
                                  "
                                  >No Command Configured</code
                                >
                              </div>
                              <div class="d-flex flex-wrap gap-2 align-items-center mt-auto">
                                <button
                                  type="button"
                                  class="btn btn-sm btn-outline-primary border-0 shadow-sm fw-medium"
                                  style="border-radius: 8px; padding: 6px 12px"
                                  @click="editCleanupCommandOrCreate(actionIdx, displayIdx)"
                                >
                                  <i class="fas fa-edit me-1" /> Edit
                                </button>
                                <button
                                  v-if="
                                    action.action.startup_commands.commands &&
                                    action.action.startup_commands.commands[displayIdx] &&
                                    action.action.startup_commands.commands[displayIdx].cmd &&
                                    action.action.startup_commands.commands[displayIdx].cmd.trim() !== ''
                                  "
                                  type="button"
                                  class="btn btn-sm btn-outline-danger border-0 shadow-sm fw-medium ms-2"
                                  style="border-radius: 8px; padding: 6px 12px"
                                  @click="showDeleteConfirmation('cleanup', displayIdx, actionIdx)"
                                >
                                  <i class="fas fa-trash me-1" /> Delete
                                </button>
                              </div>
                            </div>
                          </div>
                        </div>
                      </template>
                    </div>
                  </div>
                </div>
              </div>
            </div>
            <!-- end card-body -->
          </div>
          <!-- end card -->
        </div>
      </template>
      <template v-else>
        <div class="empty-state text-center py-5">
          <i class="fas fa-bolt text-primary mb-3" style="font-size: 2rem; opacity: 0.5" />
          <p class="text-muted mb-0" style="font-size: 1.1rem">No event actions configured yet</p>
        </div>
      </template>
    </div>

    <!-- Cleanup Commands Section (Editor/Modal) -->
    <!-- This block is handled inside the modal, not here. -->

    <!-- Modal dialog and backdrop -->
    <div v-show="showNewActionModal && currentAction">
      <!-- DEBUG: Modal visibility -->
      <div style="display: none">
        {{
          (() => {
            console.log('Modal should be visible:', showNewActionModal)
            console.log('currentAction:', currentAction)
            return ''
          })()
        }}
      </div>
      <div class="modal show d-block" tabindex="-1">
        <div class="modal-dialog modal-lg">
          <div
            v-if="currentAction"
            class="modal-content border-0 shadow-lg"
            style="border-radius: 16px"
          >
            <div
              class="modal-header border-0 pb-0"
              style="
                background: linear-gradient(135deg, #f8f9fa 0%, #e9ecef 100%);
                border-radius: 16px 16px 0 0;
              "
            >
              <div class="d-flex align-items-center">
                <div class="icon-wrapper me-3 p-2 rounded-circle bg-primary bg-opacity-10">
                  <i class="fas fa-cog text-primary" />
                </div>
                <h5 class="modal-title mb-0 fw-bold">
                  {{
                    editingAction ? $t('event_actions.edit_action') : $t('event_actions.new_action')
                  }}
                </h5>
              </div>
              <button
                type="button"
                class="btn-close btn-close-white"
                style="background: none; opacity: 0.8"
                @click="closeActionModal"
              />
            </div>
            <div class="modal-body modal-body-scrollable p-4">
              <form @submit.prevent="saveCurrentAction">
                <!-- Action Name -->
                <div class="mb-4">
                  <label for="actionName" class="form-label fw-semibold text-dark">{{
                    $t('event_actions.action_name')
                  }}</label>
                  <input
                    id="actionName"
                    v-model="currentAction.name"
                    type="text"
                    class="form-control shadow-sm border-0"
                    :placeholder="$t('event_actions.action_name_placeholder')"
                    required
                    style="border-radius: 8px; background: #f8f9fa; padding: 12px 16px"
                  />
                </div>

                <!-- Startup Stage Selection -->
                <div class="mb-4">
                  <label for="startupStage" class="form-label fw-semibold text-dark"
                    >When should the startup commands run?</label
                  >
                  <select
                    id="startupStage"
                    v-model="currentAction.action.startup_stage"
                    class="form-select shadow-sm border-0"
                    style="border-radius: 8px; background: #f8f9fa; padding: 12px 16px"
                  >
                    <option value="">Select when to run startup commands</option>
                    <option
                      v-for="stage in startupStageOptions"
                      :key="stage.stage"
                      :value="stage.stage"
                    >
                      {{ stage.name }} — {{ stage.description }}
                    </option>
                  </select>
                </div>

                <!-- Startup Commands Section -->
                <div class="mb-5">
                  <div
                    class="section-header p-3 rounded-3 mb-3"
                    style="
                      background: linear-gradient(135deg, #f0fdf4 0%, #dcfce7 100%);
                      border-left: 4px solid #10b981;
                    "
                  >
                    <h6 class="mb-0 text-success fw-bold d-flex align-items-center">
                      <i class="fas fa-play-circle me-2" /> Startup Commands
                    </h6>
                  </div>
                  <!-- Startup Failure Policy -->
                  <div class="mb-3">
                    <label for="startupFailurePolicy" class="form-label fw-semibold text-dark">{{
                      $t('event_actions.failure_policy')
                    }}</label>
                    <select
                      id="startupFailurePolicy"
                      v-model="currentAction.action.startup_commands.failure_policy"
                      class="form-select shadow-sm border-0"
                      style="border-radius: 8px; background: #f8f9fa; padding: 12px 16px"
                    >
                      <option value="FAIL_FAST">
                        {{ $t('event_actions.fail_fast') }}
                      </option>
                      <option value="CONTINUE_ON_FAILURE">
                        {{ $t('event_actions.continue_on_failure') }}
                      </option>
                    </select>
                  </div>
                  <!-- Startup & Cleanup Commands List (padded to max length) -->
                  <div class="commands-section scrollable-commands">
                    <template
                      v-for="index in Math.max(
                        currentAction.action.startup_commands.commands.length,
                        currentAction.action.cleanup_commands.commands.length || 0,
                        1
                      )"
                    >
                      <div
                        v-if="ensureStartupCommand(index - 1)"
                        :key="'startup-' + (index - 1)"
                        class="command-item mb-4 p-4 border-0 rounded-3 shadow-sm"
                        style="background: linear-gradient(135deg, #ffffff 0%, #f8f9fa 100%)"
                      >
                        <div class="row">
                          <div class="col-12 mb-3">
                            <label
                              :for="'startupCmd' + (index - 1)"
                              class="form-label fw-semibold text-dark"
                              >{{ $t('event_actions.command') }}</label
                            >
                            <input
                              :id="'startupCmd' + (index - 1)"
                              v-model="
                                currentAction.action.startup_commands.commands[index - 1].cmd
                              "
                              type="text"
                              class="form-control font-monospace shadow-sm border-0"
                              :placeholder="$t('event_actions.command_placeholder')"
                              style="border-radius: 8px; background: #f1f5f9; padding: 12px 16px"
                            />
                          </div>
                          <div class="col-md-6">
                            <div class="form-check mb-3">
                              <input
                                :id="'startupElevated' + (index - 1)"
                                v-model="
                                  currentAction.action.startup_commands.commands[index - 1].elevated
                                "
                                class="form-check-input"
                                type="checkbox"
                                style="border-radius: 4px"
                              />
                              <label
                                class="form-check-label fw-medium"
                                :for="'startupElevated' + (index - 1)"
                              >
                                {{ $t('event_actions.elevated') }}
                              </label>
                            </div>
                            <div class="form-check mb-3">
                              <input
                                :id="'startupAsync' + (index - 1)"
                                v-model="
                                  currentAction.action.startup_commands.commands[index - 1].async
                                "
                                class="form-check-input"
                                type="checkbox"
                                style="border-radius: 4px"
                              />
                              <label
                                class="form-check-label fw-medium"
                                :for="'startupAsync' + (index - 1)"
                              >
                                {{ $t('event_actions.async') }}
                              </label>
                            </div>
                          </div>
                          <div class="col-md-6">
                            <div class="form-check mb-3">
                              <input
                                :id="'startupIgnoreError' + (index - 1)"
                                v-model="
                                  currentAction.action.startup_commands.commands[index - 1]
                                    .ignore_error
                                "
                                class="form-check-input"
                                type="checkbox"
                                style="border-radius: 4px"
                              />
                              <label
                                class="form-check-label fw-medium"
                                :for="'startupIgnoreError' + (index - 1)"
                              >
                                {{ $t('event_actions.ignore_error') }}
                              </label>
                            </div>
                            <div>
                              <label
                                :for="'startupTimeout' + (index - 1)"
                                class="form-label fw-semibold text-dark"
                                >{{ $t('event_actions.timeout') }}</label
                              >
                              <input
                                :id="'startupTimeout' + (index - 1)"
                                v-model.number="
                                  currentAction.action.startup_commands.commands[index - 1]
                                    .timeout_seconds
                                "
                                type="number"
                                class="form-control shadow-sm border-0"
                                min="1"
                                max="3600"
                                style="border-radius: 8px; background: #f8f9fa; padding: 12px 16px"
                              />
                            </div>
                          </div>
                        </div>
                        <div class="text-end mt-3">
                          <button
                            type="button"
                            class="btn btn-sm btn-outline-danger border-0 shadow-sm fw-medium"
                            style="border-radius: 8px; padding: 8px 16px"
                            @click="showDeleteConfirmation('startup', index - 1)"
                          >
                            <i class="fas fa-trash me-1" /> Remove
                          </button>
                        </div>
                      </div>
                    </template>
                    <button
                      type="button"
                      class="btn btn-outline-success btn-sm shadow-sm border-0 fw-medium"
                      style="border-radius: 10px; padding: 12px 20px"
                      @click="addStartupCommand"
                    >
                      <i class="fas fa-plus me-2" /> Add Startup Command
                    </button>
                  </div>
                </div>

                <!-- Shutdown Stage Selection -->
                <div class="mb-4">
                  <label for="shutdownStage" class="form-label fw-semibold text-dark"
                    >When should the cleanup commands run?</label
                  >
                  <select
                    id="shutdownStage"
                    v-model="currentAction.action.shutdown_stage"
                    class="form-select shadow-sm border-0"
                    style="border-radius: 8px; background: #f8f9fa; padding: 12px 16px"
                  >
                    <option value="">Select when to run cleanup commands</option>
                    <option
                      v-for="stage in cleanupStageOptions"
                      :key="stage.stage"
                      :value="stage.stage"
                    >
                      {{ stage.name }} — {{ stage.description }}
                    </option>
                  </select>
                </div>

                <!-- Cleanup Commands Section -->
                <div class="mb-5">
                  <div
                    class="section-header p-3 rounded-3 mb-3"
                    style="
                      background: linear-gradient(135deg, #fffbeb 0%, #fef3c7 100%);
                      border-left: 4px solid #f59e0b;
                    "
                  >
                    <h6 class="mb-2 text-warning fw-bold d-flex align-items-center">
                      <i class="fas fa-undo me-2" /> Cleanup Commands
                    </h6>
                    <div
                      class="alert alert-info py-2 px-3 mb-0 border-0"
                      style="background: rgba(59, 130, 246, 0.1); border-radius: 8px"
                    >
                      <i class="fas fa-info-circle me-2 text-info" />
                      <small class="text-dark"
                        >Cleanup commands are <strong>automatically reversed</strong> when run.
                        Please add them in the same order as your startup commands (e.g., if startup
                        is [A, B, C], add cleanup as [A, B, C]; they will run as [C, B, A]).</small
                      >
                    </div>
                  </div>
                  <!-- Cleanup Failure Policy -->
                  <div class="mb-3">
                    <label for="cleanupFailurePolicy" class="form-label fw-semibold text-dark">{{
                      $t('event_actions.failure_policy')
                    }}</label>
                    <select
                      id="cleanupFailurePolicy"
                      v-model="currentAction.action.cleanup_commands.failure_policy"
                      class="form-select shadow-sm border-0"
                      style="border-radius: 8px; background: #f8f9fa; padding: 12px 16px"
                    >
                      <option value="FAIL_FAST">
                        {{ $t('event_actions.fail_fast') }}
                      </option>
                      <option value="CONTINUE_ON_FAILURE">
                        {{ $t('event_actions.continue_on_failure') }}
                      </option>
                    </select>
                  </div>
                  <div class="commands-section scrollable-commands">
                    <template
                      v-for="index in Math.max(
                        currentAction.action.startup_commands.commands.length,
                        currentAction.action.cleanup_commands.commands.length || 0,
                        1
                      )"
                    >
                      <div
                        v-if="ensureCleanupCommand(index - 1)"
                        :key="'cleanup-' + (index - 1)"
                        class="command-item mb-4 p-4 border-0 rounded-3 shadow-sm"
                        style="background: linear-gradient(135deg, #ffffff 0%, #f8f9fa 100%)"
                      >
                        <div class="row">
                          <div class="col-12 mb-3">
                            <label
                              :for="'cleanupCmd' + (index - 1)"
                              class="form-label fw-semibold text-dark"
                              >{{ $t('event_actions.command') }}</label
                            >
                            <input
                              :id="'cleanupCmd' + (index - 1)"
                              v-model="
                                currentAction.action.cleanup_commands.commands[index - 1].cmd
                              "
                              type="text"
                              class="form-control font-monospace shadow-sm border-0"
                              :placeholder="$t('event_actions.command_placeholder')"
                              style="border-radius: 8px; background: #f1f5f9; padding: 12px 16px"
                            />
                          </div>
                          <div class="col-md-6">
                            <div class="form-check mb-3">
                              <input
                                :id="'cleanupElevated' + (index - 1)"
                                v-model="
                                  currentAction.action.cleanup_commands.commands[index - 1].elevated
                                "
                                class="form-check-input"
                                type="checkbox"
                                style="border-radius: 4px"
                              />
                              <label
                                class="form-check-label fw-medium"
                                :for="'cleanupElevated' + (index - 1)"
                              >
                                {{ $t('event_actions.elevated') }}
                              </label>
                            </div>
                            <div class="form-check mb-3">
                              <input
                                :id="'cleanupAsync' + (index - 1)"
                                v-model="
                                  currentAction.action.cleanup_commands.commands[index - 1].async
                                "
                                class="form-check-input"
                                type="checkbox"
                                style="border-radius: 4px"
                              />
                              <label
                                class="form-check-label fw-medium"
                                :for="'cleanupAsync' + (index - 1)"
                              >
                                {{ $t('event_actions.async') }}
                              </label>
                            </div>
                          </div>
                          <div class="col-md-6">
                            <div class="form-check mb-3">
                              <input
                                :id="'cleanupIgnoreError' + (index - 1)"
                                v-model="
                                  currentAction.action.cleanup_commands.commands[index - 1]
                                    .ignore_error
                                "
                                class="form-check-input"
                                type="checkbox"
                                style="border-radius: 4px"
                              />
                              <label
                                class="form-check-label fw-medium"
                                :for="'cleanupIgnoreError' + (index - 1)"
                              >
                                {{ $t('event_actions.ignore_error') }}
                              </label>
                            </div>
                            <div>
                              <label
                                :for="'cleanupTimeout' + (index - 1)"
                                class="form-label fw-semibold text-dark"
                                >{{ $t('event_actions.timeout') }}</label
                              >
                              <input
                                :id="'cleanupTimeout' + (index - 1)"
                                v-model.number="
                                  currentAction.action.cleanup_commands.commands[index - 1]
                                    .timeout_seconds
                                "
                                type="number"
                                class="form-control shadow-sm border-0"
                                min="1"
                                max="3600"
                                style="border-radius: 8px; background: #f8f9fa; padding: 12px 16px"
                              />
                            </div>
                          </div>
                        </div>
                        <div class="text-end mt-3">
                          <button
                            type="button"
                            class="btn btn-sm btn-outline-danger border-0 shadow-sm fw-medium"
                            style="border-radius: 8px; padding: 8px 16px"
                            @click="showDeleteConfirmation('cleanup', index - 1)"
                          >
                            <i class="fas fa-trash me-1" /> Remove
                          </button>
                        </div>
                      </div>
                    </template>
                    <button
                      type="button"
                      class="btn btn-outline-warning btn-sm shadow-sm border-0 fw-medium"
                      style="border-radius: 10px; padding: 12px 20px"
                      @click="addCleanupCommand"
                    >
                      <i class="fas fa-plus me-2" /> Add Cleanup Command
                    </button>
                  </div>
                </div>
              </form>
            </div>
            <div
              class="modal-footer border-0 p-4"
              style="background: #f8f9fa; border-radius: 0 0 16px 16px"
            >
              <button
                type="button"
                class="btn btn-light me-3 shadow-sm fw-medium"
                style="border-radius: 10px; padding: 12px 24px"
                @click="closeActionModal"
              >
                Cancel
              </button>
              <button
                type="button"
                class="btn btn-primary shadow-sm fw-semibold"
                style="
                  border-radius: 10px;
                  padding: 12px 24px;
                  background: linear-gradient(135deg, #0d6efd 0%, #6610f2 100%);
                  border: none;
                "
                @click="saveCurrentAction"
              >
                Save Action
              </button>
            </div>
          </div>
        </div>
      </div>
      <div class="modal-backdrop show" />
    </div>

    <!-- Edit Command Modal -->
    <div v-if="showEditCommandModal">
      <div class="modal show d-block" tabindex="-1">
        <div class="modal-dialog modal-xl">
          <div class="modal-content border-0 shadow-lg" style="border-radius: 16px">
            <div
              class="modal-header border-0 pb-0"
              style="
                background: linear-gradient(135deg, #f8f9fa 0%, #e9ecef 100%);
                border-radius: 16px 16px 0 0;
              "
            >
              <div class="d-flex align-items-center">
                <div class="icon-wrapper me-3 p-2 rounded-circle bg-primary bg-opacity-10">
                  <i class="fas fa-edit text-primary" />
                </div>
                <h5 class="modal-title mb-0 fw-bold">Edit Commands</h5>
              </div>
              <button
                type="button"
                class="btn-close btn-close-white"
                style="background: none; opacity: 0.8"
                @click="closeEditCommandModal"
              />
            </div>
            <div class="modal-body modal-body-scrollable p-4">
              <form @submit.prevent="saveEditCommand">
                <div class="row">
                  <!-- Startup Command Section -->
                  <div class="col-lg-6">
                    <div
                      class="border rounded-3 p-4 h-100"
                      style="background: #f8fffe; border-color: #d1ecf1"
                    >
                      <div class="d-flex align-items-center mb-3">
                        <div class="icon-wrapper me-2 p-1 rounded-circle bg-success bg-opacity-10">
                          <i class="fas fa-play text-success" style="font-size: 0.9rem" />
                        </div>
                        <h6 class="mb-0 fw-bold text-success">Startup Command</h6>
                      </div>
                      <div class="mb-3">
                        <label class="form-label fw-semibold text-dark">Command</label>
                        <textarea
                          v-model="editCommandData.startup.cmd"
                          class="form-control font-monospace shadow-sm border-0"
                          rows="4"
                          style="
                            border-radius: 8px;
                            background: #f1f5f9;
                            padding: 12px 16px;
                            resize: vertical;
                            min-height: 120px;
                          "
                          placeholder="Enter startup command here..."
                        />
                      </div>
                      <div class="mb-3">
                        <label class="form-label fw-semibold text-dark">Timeout (seconds)</label>
                        <input
                          v-model.number="editCommandData.startup.timeout_seconds"
                          type="number"
                          class="form-control shadow-sm border-0"
                          min="1"
                          max="3600"
                          style="border-radius: 8px; background: #f8f9fa; padding: 12px 16px"
                        />
                      </div>
                      <div class="mb-0">
                        <label class="form-label fw-semibold text-dark">Options</label>
                        <div class="bg-light p-3 rounded-3" style="background: #f8f9fa">
                          <div class="form-check mb-2">
                            <input
                              id="editStartupElevated"
                              v-model="editCommandData.startup.elevated"
                              class="form-check-input"
                              type="checkbox"
                              style="border-radius: 4px"
                            />
                            <label class="form-check-label fw-medium" for="editStartupElevated">
                              <i class="fas fa-shield-alt me-2 text-warning" />Elevated
                            </label>
                          </div>
                          <div class="form-check mb-2">
                            <input
                              id="editStartupAsync"
                              v-model="editCommandData.startup.async"
                              class="form-check-input"
                              type="checkbox"
                              style="border-radius: 4px"
                            />
                            <label class="form-check-label fw-medium" for="editStartupAsync">
                              <i class="fas fa-play me-2 text-primary" />Async
                            </label>
                          </div>
                          <div class="form-check mb-0">
                            <input
                              id="editStartupIgnoreError"
                              v-model="editCommandData.startup.ignore_error"
                              class="form-check-input"
                              type="checkbox"
                              style="border-radius: 4px"
                            />
                            <label class="form-check-label fw-medium" for="editStartupIgnoreError">
                              <i class="fas fa-exclamation-triangle me-2 text-secondary" />Ignore
                              errors
                            </label>
                          </div>
                        </div>
                      </div>
                    </div>
                  </div>

                  <!-- Cleanup Command Section -->
                  <div class="col-lg-6">
                    <div
                      class="border rounded-3 p-4 h-100"
                      style="background: #fff8f0; border-color: #fec107"
                    >
                      <div class="d-flex align-items-center mb-3">
                        <div class="icon-wrapper me-2 p-1 rounded-circle bg-warning bg-opacity-10">
                          <i class="fas fa-stop text-warning" style="font-size: 0.9rem" />
                        </div>
                        <h6 class="mb-0 fw-bold text-warning">Cleanup Command</h6>
                      </div>
                      <div class="mb-3">
                        <label class="form-label fw-semibold text-dark">Command</label>
                        <textarea
                          v-model="editCommandData.cleanup.cmd"
                          class="form-control font-monospace shadow-sm border-0"
                          rows="4"
                          style="
                            border-radius: 8px;
                            background: #f1f5f9;
                            padding: 12px 16px;
                            resize: vertical;
                            min-height: 120px;
                          "
                          placeholder="Enter cleanup command here..."
                        />
                      </div>
                      <div class="mb-3">
                        <label class="form-label fw-semibold text-dark">Timeout (seconds)</label>
                        <input
                          v-model.number="editCommandData.cleanup.timeout_seconds"
                          type="number"
                          class="form-control shadow-sm border-0"
                          min="1"
                          max="3600"
                          style="border-radius: 8px; background: #f8f9fa; padding: 12px 16px"
                        />
                      </div>
                      <div class="mb-0">
                        <label class="form-label fw-semibold text-dark">Options</label>
                        <div class="bg-light p-3 rounded-3" style="background: #f8f9fa">
                          <div class="form-check mb-2">
                            <input
                              id="editCleanupElevated"
                              v-model="editCommandData.cleanup.elevated"
                              class="form-check-input"
                              type="checkbox"
                              style="border-radius: 4px"
                            />
                            <label class="form-check-label fw-medium" for="editCleanupElevated">
                              <i class="fas fa-shield-alt me-2 text-warning" />Elevated
                            </label>
                          </div>
                          <div class="form-check mb-2">
                            <input
                              id="editCleanupAsync"
                              v-model="editCommandData.cleanup.async"
                              class="form-check-input"
                              type="checkbox"
                              style="border-radius: 4px"
                            />
                            <label class="form-check-label fw-medium" for="editCleanupAsync">
                              <i class="fas fa-play me-2 text-primary" />Async
                            </label>
                          </div>
                          <div class="form-check mb-0">
                            <input
                              id="editCleanupIgnoreError"
                              v-model="editCommandData.cleanup.ignore_error"
                              class="form-check-input"
                              type="checkbox"
                              style="border-radius: 4px"
                            />
                            <label class="form-check-label fw-medium" for="editCleanupIgnoreError">
                              <i class="fas fa-exclamation-triangle me-2 text-secondary" />Ignore
                              errors
                            </label>
                          </div>
                        </div>
                      </div>
                    </div>
                  </div>
                </div>

                <div class="d-flex justify-content-end gap-3 mt-4">
                  <button
                    type="button"
                    class="btn btn-light shadow-sm fw-medium"
                    style="border-radius: 10px; padding: 12px 20px"
                    @click="closeEditCommandModal"
                  >
                    Cancel
                  </button>
                  <button
                    type="submit"
                    class="btn btn-primary shadow-sm fw-semibold"
                    style="
                      border-radius: 10px;
                      padding: 12px 20px;
                      background: linear-gradient(135deg, #0d6efd 0%, #6610f2 100%);
                      border: none;
                    "
                  >
                    Save Changes
                  </button>
                </div>
              </form>
            </div>
          </div>
        </div>
      </div>
      <div class="modal-backdrop show" />
    </div>

    <!-- Delete Confirmation Modal -->
    <div v-if="showConfirmDeleteModal">
      <div class="modal show d-block" tabindex="-1">
        <div class="modal-dialog modal-lg">
          <div class="modal-content border-0 shadow-lg" style="border-radius: 16px">
            <div
              class="modal-header border-0 pb-0"
              style="
                background: linear-gradient(135deg, #fff5f5 0%, #fed7d7 100%);
                border-radius: 16px 16px 0 0;
              "
            >
              <div class="d-flex align-items-center">
                <div class="icon-wrapper me-3 p-2 rounded-circle bg-danger bg-opacity-10">
                  <i class="fas fa-exclamation-triangle text-danger" />
                </div>
                <h5 class="modal-title mb-0 fw-bold text-danger">Confirm Deletion</h5>
              </div>
              <button
                type="button"
                class="btn-close"
                style="background: none; opacity: 0.8"
                @click="closeDeleteConfirmation"
              />
            </div>
            <div class="modal-body p-4">
              <div class="alert alert-warning border-0 mb-4" style="background: #fff3cd; border-radius: 12px">
                <div class="d-flex align-items-start">
                  <i class="fas fa-exclamation-triangle text-warning me-3" style="font-size: 1.5rem; margin-top: 0.1rem" />
                  <div>
                    <h6 class="alert-heading mb-2 text-warning fw-bold">Warning: This will delete both commands!</h6>
                    <p class="mb-0 text-dark">
                      Deleting this command will remove both the startup and cleanup commands at this position.
                      This action cannot be undone.
                    </p>
                  </div>
                </div>
              </div>

              <div class="row">
                <!-- Startup Command Preview -->
                <div class="col-md-6" v-if="deleteConfirmationData.startupCommand">
                  <div class="border rounded-3 p-3" style="background: #f0fdf4; border-color: #d1fae5">
                    <h6 class="text-success mb-2 fw-bold d-flex align-items-center">
                      <i class="fas fa-play-circle me-2" />
                      Startup Command to Delete
                    </h6>
                    <code class="d-block bg-dark text-light p-3 rounded-3 border-0" style="
                      font-size: 0.85rem;
                      line-height: 1.4;
                      max-height: 100px;
                      overflow: auto;
                      word-break: break-all;
                      white-space: pre-wrap;
                    ">{{ deleteConfirmationData.startupCommand || 'No command configured' }}</code>
                  </div>
                </div>

                <!-- Cleanup Command Preview -->
                <div class="col-md-6" v-if="deleteConfirmationData.cleanupCommand">
                  <div class="border rounded-3 p-3" style="background: #fffbeb; border-color: #fef3c7">
                    <h6 class="text-warning mb-2 fw-bold d-flex align-items-center">
                      <i class="fas fa-undo me-2" />
                      Cleanup Command to Delete
                    </h6>
                    <code class="d-block bg-dark text-light p-3 rounded-3 border-0" style="
                      font-size: 0.85rem;
                      line-height: 1.4;
                      max-height: 100px;
                      overflow: auto;
                      word-break: break-all;
                      white-space: pre-wrap;
                    ">{{ deleteConfirmationData.cleanupCommand || 'No command configured' }}</code>
                  </div>
                </div>

                <!-- Single column if only one command -->
                <div class="col-12" v-if="!deleteConfirmationData.startupCommand && !deleteConfirmationData.cleanupCommand">
                  <div class="border rounded-3 p-3" style="background: #f8f9fa; border-color: #dee2e6">
                    <h6 class="text-muted mb-2 fw-bold d-flex align-items-center">
                      <i class="fas fa-info-circle me-2" />
                      No Commands Configured
                    </h6>
                    <p class="mb-0 text-muted">No commands are currently configured at this position.</p>
                  </div>
                </div>
              </div>
            </div>
            <div
              class="modal-footer border-0 p-4"
              style="background: #f8f9fa; border-radius: 0 0 16px 16px"
            >
              <button
                type="button"
                class="btn btn-light me-3 shadow-sm fw-medium"
                style="border-radius: 10px; padding: 12px 24px"
                @click="closeDeleteConfirmation"
              >
                Cancel
              </button>
              <button
                type="button"
                class="btn btn-danger shadow-sm fw-semibold"
                style="
                  border-radius: 10px;
                  padding: 12px 24px;
                  background: linear-gradient(135deg, #dc3545 0%, #c62828 100%);
                  border: none;
                "
                @click="confirmDelete"
              >
                <i class="fas fa-trash me-2" />Delete Commands
              </button>
            </div>
          </div>
        </div>
      </div>
      <div class="modal-backdrop show" />
    </div>
  </div>
</template>

<script setup>
import { ref, onUnmounted } from 'vue'
import useEventActionsEditor from './EventActionsEditor'

function getTruncatedCommand(text, n = 90) {
  if (!text || text.length <= n) return text // already short enough
  return '...' + text.slice(-n) // last n characters
}
// Helper to ensure startup command exists at index
function ensureStartupCommand(index) {
  if (!currentAction.value?.action?.startup_commands?.commands) return false

  // Ensure the array is long enough
  while (currentAction.value.action.startup_commands.commands.length <= index) {
    currentAction.value.action.startup_commands.commands.push({
      cmd: '',
      elevated: false,
      async: false,
      ignore_error: false,
      timeout_seconds: 30,
    })
  }
  return true
}

// Helper to ensure cleanup command exists at index
function ensureCleanupCommand(index) {
  if (!currentAction.value?.action?.cleanup_commands?.commands) return false

  // Ensure the array is long enough
  while (currentAction.value.action.cleanup_commands.commands.length <= index) {
    currentAction.value.action.cleanup_commands.commands.push({
      cmd: '',
      elevated: false,
      async: false,
      ignore_error: false,
      timeout_seconds: 30,
    })
  }
  return true
}

const props = defineProps({
  modelValue: {
    type: Array,
    default: () => [],
  },
})

const emit = defineEmits(['update:modelValue'])

// Edit command modal state
const showEditCommandModal = ref(false)
const editCommandData = ref({
  startup: {},
  cleanup: {},
})
const editCommandMeta = ref({})

// Confirmation modal state
const showConfirmDeleteModal = ref(false)
const deleteConfirmationData = ref({
  type: '', // 'startup' or 'cleanup'
  index: -1,
  actionIndex: -1,
  startupCommand: '',
  cleanupCommand: ''
})

function editStartupCommand(actionIdx, cmdIdx) {
  const startupCmd = JSON.parse(
    JSON.stringify(eventActions.value[actionIdx].action.startup_commands.commands[cmdIdx])
  )

  // Find corresponding cleanup command (if any)
  const cleanupCommands = eventActions.value[actionIdx].action.cleanup_commands.commands || []
  const cleanupIdx = cleanupCommands.length - 1 - cmdIdx // Cleanup commands are in reverse order
  const cleanupCmd =
    cleanupIdx >= 0 && cleanupIdx < cleanupCommands.length
      ? JSON.parse(JSON.stringify(cleanupCommands[cleanupIdx]))
      : { cmd: '', elevated: false, async: false, ignore_error: false, timeout_seconds: 30 }

  showEditCommandModal.value = true
  editCommandData.value = {
    startup: startupCmd,
    cleanup: cleanupCmd,
  }
  editCommandMeta.value = { type: 'startup', actionIdx, cmdIdx }
}

function editStartupCommandOrCreate(actionIdx, cmdIdx) {
  // Ensure the commands array has enough elements
  while (eventActions.value[actionIdx].action.startup_commands.commands.length <= cmdIdx) {
    eventActions.value[actionIdx].action.startup_commands.commands.push({
      cmd: '',
      elevated: false,
      async: false,
      ignore_error: false,
      timeout_seconds: 30,
    })
  }
  editStartupCommand(actionIdx, cmdIdx)
}

function editCleanupCommand(actionIdx, cmdIdx) {
  // Note: cleanup commands are shown in reverse order
  const realIdx = eventActions.value[actionIdx].action.cleanup_commands.commands.length - 1 - cmdIdx
  const cleanupCmd = JSON.parse(
    JSON.stringify(eventActions.value[actionIdx].action.cleanup_commands.commands[realIdx])
  )

  // Find corresponding startup command (if any)
  const startupCommands = eventActions.value[actionIdx].action.startup_commands.commands || []
  const startupIdx = cmdIdx // Direct mapping to startup command index
  const startupCmd =
    startupIdx >= 0 && startupIdx < startupCommands.length
      ? JSON.parse(JSON.stringify(startupCommands[startupIdx]))
      : { cmd: '', elevated: false, async: false, ignore_error: false, timeout_seconds: 30 }

  showEditCommandModal.value = true
  editCommandData.value = {
    startup: startupCmd,
    cleanup: cleanupCmd,
  }
  editCommandMeta.value = { type: 'cleanup', actionIdx, cmdIdx: realIdx }
}

function editCleanupCommandOrCreate(actionIdx, cmdIdx) {
  // Ensure the commands array has enough elements
  while (eventActions.value[actionIdx].action.cleanup_commands.commands.length <= cmdIdx) {
    eventActions.value[actionIdx].action.cleanup_commands.commands.push({
      cmd: '',
      elevated: false,
      async: false,
      ignore_error: false,
      timeout_seconds: 30,
    })
  }
  editCleanupCommand(actionIdx, cmdIdx)
}
function closeEditCommandModal() {
  showEditCommandModal.value = false
  editCommandData.value = { startup: {}, cleanup: {} }
  editCommandMeta.value = {}
}

function saveEditCommand() {
  const { type, actionIdx, cmdIdx } = editCommandMeta.value

  // Save both startup and cleanup commands, ensuring arrays exist
  const action = eventActions.value[actionIdx].action

  // Ensure startup commands array exists
  if (!action.startup_commands) {
    action.startup_commands = { commands: [] }
  }

  // Ensure cleanup commands array exists
  if (!action.cleanup_commands) {
    action.cleanup_commands = { commands: [] }
  }

  // Save startup command
  if (editCommandData.value.startup.cmd && editCommandData.value.startup.cmd.trim()) {
    // Ensure startup array is large enough
    const startupIdx =
      type === 'startup'
        ? cmdIdx
        : type === 'cleanup'
          ? eventActions.value[actionIdx].action.cleanup_commands.commands.length - 1 - cmdIdx
          : cmdIdx

    while (action.startup_commands.commands.length <= startupIdx) {
      action.startup_commands.commands.push({
        cmd: '',
        elevated: false,
        async: false,
        ignore_error: false,
        timeout_seconds: 30,
      })
    }
    action.startup_commands.commands[startupIdx] = { ...editCommandData.value.startup }
  }

  // Save cleanup command
  if (editCommandData.value.cleanup.cmd && editCommandData.value.cleanup.cmd.trim()) {
    // Ensure cleanup array is large enough
    const cleanupIdx =
      type === 'cleanup'
        ? cmdIdx
        : type === 'startup'
          ? action.cleanup_commands.commands.length - 1 - cmdIdx
          : 0

    while (action.cleanup_commands.commands.length <= cleanupIdx) {
      action.cleanup_commands.commands.push({
        cmd: '',
        elevated: false,
        async: false,
        ignore_error: false,
        timeout_seconds: 30,
      })
    }
    action.cleanup_commands.commands[cleanupIdx] = { ...editCommandData.value.cleanup }
  }

  closeEditCommandModal()
}

// Confirmation modal functions
function showDeleteConfirmation(type, index, actionIndex = null) {
  const action = actionIndex !== null ? eventActions.value[actionIndex] : currentAction.value
  if (!action) return

  let startupCmd = ''
  let cleanupCmd = ''

  if (type === 'startup') {
    // Get startup command
    if (action.action.startup_commands.commands[index]) {
      startupCmd = action.action.startup_commands.commands[index].cmd || ''
    }
    // Get corresponding cleanup command (reverse order)
    const cleanupCommands = action.action.cleanup_commands.commands || []
    const cleanupIdx = cleanupCommands.length - 1 - index
    if (cleanupIdx >= 0 && cleanupIdx < cleanupCommands.length) {
      cleanupCmd = cleanupCommands[cleanupIdx].cmd || ''
    }
  } else if (type === 'cleanup') {
    // Get cleanup command (index is display index, need to convert to real index)
    const cleanupCommands = action.action.cleanup_commands.commands || []
    const realIdx = cleanupCommands.length - 1 - index
    if (realIdx >= 0 && realIdx < cleanupCommands.length) {
      cleanupCmd = cleanupCommands[realIdx].cmd || ''
    }
    // Get corresponding startup command (display index maps directly to startup index)
    if (action.action.startup_commands.commands[index]) {
      startupCmd = action.action.startup_commands.commands[index].cmd || ''
    }
  }

  deleteConfirmationData.value = {
    type,
    index,
    actionIndex,
    startupCommand: startupCmd,
    cleanupCommand: cleanupCmd
  }
  showConfirmDeleteModal.value = true
}

function confirmDelete() {
  const { type, index, actionIndex } = deleteConfirmationData.value
  
  if (actionIndex !== null) {
    // Deleting from main view - modify eventActions directly
    const action = eventActions.value[actionIndex]
    if (!action) return
    
    let startupIndex, cleanupIndex
    
    if (type === 'startup') {
      // Clicked delete on startup side - index is startup index
      startupIndex = index
      // For cleanup: if deleting startup command 0, delete cleanup command at last index
      // if deleting startup command 1, delete cleanup command at second-to-last index, etc.
      cleanupIndex = action.action.cleanup_commands.commands.length - 1 - index
    } else {
      // Clicked delete on cleanup side - index is cleanup display index (already reversed)
      // The display index corresponds directly to the startup index
      startupIndex = index
      // For cleanup: if display shows index 0 (first cleanup command), it's actually the last in array
      cleanupIndex = action.action.cleanup_commands.commands.length - 1 - index
    }
    
    // Delete startup command at startupIndex
    if (startupIndex >= 0 && startupIndex < action.action.startup_commands.commands.length) {
      action.action.startup_commands.commands.splice(startupIndex, 1)
    }
    
    // Delete cleanup command at cleanupIndex
    if (cleanupIndex >= 0 && cleanupIndex < action.action.cleanup_commands.commands.length) {
      action.action.cleanup_commands.commands.splice(cleanupIndex, 1)
    }
  } else {
    // Deleting from modal - use currentAction
    const action = currentAction.value
    if (!action) return
    
    let startupIndex, cleanupIndex
    
    if (type === 'startup') {
      // Clicked delete on startup side - index is startup index
      startupIndex = index
      // For cleanup: if deleting startup command 0, delete cleanup command at last index
      cleanupIndex = action.action.cleanup_commands.commands.length - 1 - index
    } else {
      // Clicked delete on cleanup side - index is cleanup display index (already reversed)
      startupIndex = index
      // For cleanup: if display shows index 0 (first cleanup command), it's actually the last in array
      cleanupIndex = action.action.cleanup_commands.commands.length - 1 - index
    }
    
    // Delete startup command at startupIndex
    if (startupIndex >= 0 && startupIndex < action.action.startup_commands.commands.length) {
      action.action.startup_commands.commands.splice(startupIndex, 1)
    }
    
    // Delete cleanup command at cleanupIndex
    if (cleanupIndex >= 0 && cleanupIndex < action.action.cleanup_commands.commands.length) {
      action.action.cleanup_commands.commands.splice(cleanupIndex, 1)
    }
  }
  
  closeDeleteConfirmation()
}

function closeDeleteConfirmation() {
  showConfirmDeleteModal.value = false
  deleteConfirmationData.value = {
    type: '',
    index: -1,
    actionIndex: -1,
    startupCommand: '',
    cleanupCommand: ''
  }
}

// Handle keyboard events for confirmation modal
function handleConfirmationKeydown(event) {
  if (!showConfirmDeleteModal.value) return
  
  if (event.key === 'Enter') {
    event.preventDefault()
    confirmDelete()
  } else if (event.key === 'Escape') {
    event.preventDefault()
    closeDeleteConfirmation()
  }
}

// Add event listener for keyboard events and cleanup on unmount
if (typeof window !== 'undefined') {
  window.addEventListener('keydown', handleConfirmationKeydown)
  
  onUnmounted(() => {
    window.removeEventListener('keydown', handleConfirmationKeydown)
  })
}

const {
  eventActions,
  showNewActionModal,
  currentAction,
  editingAction,
  startupStageOptions,
  cleanupStageOptions,
  showNewAction,
  closeActionModal,
  saveCurrentAction,
  addStartupCommand,
  removeStartupCommand,
  addCleanupCommand,
  removeCleanupCommand,
  editAction,
  deleteAction,
  getStageDisplayName,
} = useEventActionsEditor(props, emit)
</script>

<style scoped>
.event-actions-editor {
  font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', 'Roboto', sans-serif;
}

/* Enhanced card styling */
.card {
  transition: all 0.3s ease;
  border: 1px solid rgba(0, 0, 0, 0.08) !important;
  background: #ffffff;
  height: 100%;
}

/* Ensure card-body stretches to fill parent */
.card-body {
  height: 100%;
  display: flex;
  flex-direction: column;
}

/* Command display improvements */
.command-item {
  transition: all 0.3s ease;
  border: 1px solid rgba(0, 0, 0, 0.08) !important;
  background: rgba(255, 255, 255, 0.95) !important;
  backdrop-filter: blur(10px);
  height: 100%;
  display: flex;
  flex-direction: column;
}

.command-item:hover {
  transform: translateY(-2px);
  box-shadow: 0 8px 20px rgba(0, 0, 0, 0.12) !important;
  border-color: rgba(13, 110, 253, 0.2) !important;
}

/* Ensure commands in same row have same height */
.commands-list {
  padding-right: 8px;
  display: flex;
  flex-direction: column;
  height: 100%;
}

.commands-list > .command-item {
  flex: 1;
  min-height: 0;
}

/* Icon wrapper styling */
.icon-wrapper {
  width: 40px;
  height: 40px;
  display: flex;
  align-items: center;
  justify-content: center;
}

/* Badge improvements */
.badge {
  font-weight: 600;
  letter-spacing: 0.025em;
  transition: all 0.2s ease;
}

/* Button enhancements */
.btn {
  transition: all 0.3s ease;
  font-weight: 500;
  position: relative;
  overflow: hidden;
}

.btn:hover {
  transform: translateY(-2px);
}

.btn-primary {
  background: linear-gradient(135deg, #0d6efd 0%, #6610f2 100%);
  border: none;
}

.btn-primary:hover {
  background: linear-gradient(135deg, #0b5ed7 0%, #5a0fc8 100%);
  box-shadow: 0 8px 25px rgba(13, 110, 253, 0.4);
}

.btn-outline-primary:hover {
  background: linear-gradient(135deg, #0d6efd 0%, #6610f2 100%);
  border-color: transparent;
}

.btn-outline-warning:hover {
  background: linear-gradient(135deg, #ffc107 0%, #fd7e14 100%);
  border-color: transparent;
}

.btn-outline-success:hover {
  background: linear-gradient(135deg, #198754 0%, #20c997 100%);
  border-color: transparent;
}

/* Modal enhancements */
.modal-content {
  backdrop-filter: blur(10px);
}

.modal-backdrop {
  background-color: rgba(0, 0, 0, 0.4);
}

/* Form control improvements */
.form-control,
.form-select {
  transition: all 0.3s ease;
  border: 2px solid rgba(0, 0, 0, 0.08) !important;
  background: rgba(248, 249, 250, 0.8) !important;
}

.form-control:focus,
.form-select:focus {
  border-color: #0d6efd !important;
  box-shadow: 0 0 0 0.25rem rgba(13, 110, 253, 0.15) !important;
  transform: translateY(-1px);
  background: #ffffff !important;
}

.form-control:hover,
.form-select:hover {
  border-color: rgba(13, 110, 253, 0.3) !important;
  background: #ffffff !important;
}

/* Section header styling */
.section-header {
  transition: all 0.2s ease;
}

/* Empty state styling */
.empty-state {
  opacity: 0.7;
}

/* Command number badge */
.command-number .badge {
  font-size: 0.75rem;
  min-width: 28px;
  height: 28px;
  display: flex;
  align-items: center;
  justify-content: center;
  font-weight: 600;
  border: 2px solid rgba(255, 255, 255, 0.3);
}

/* Commands list spacing */
.commands-list {
  padding-right: 8px;
  display: flex;
  flex-direction: column;
  height: 100%;
}

.commands-list > .command-item {
  flex: 1;
  min-height: 0;
}

/* Command display improvements */
.command-display {
  max-width: 100%;
  overflow: hidden;
  font-family:
    'SFMono-Regular', 'Monaco', 'Inconsolata', 'Roboto Mono', 'Source Code Pro', monospace !important;
}

/* Fixed height/width and wrapping for command code blocks */
.command-fixed-wrap {
  min-width: 220px;
  max-width: 100%;
  width: 100%;
  min-height: 2.8em;
  max-height: 2.8em;
  height: 2.8em;
  display: block;
  overflow: hidden;
  white-space: pre-wrap !important;
  word-break: break-all;
  text-align: right;
  /* Show only last two lines and show end of text */
  display: -webkit-box;
  -webkit-line-clamp: 2;
  -webkit-box-orient: vertical;
  line-clamp: 2;
  overflow: hidden;
  direction: ltr;
  /* Added to show the end of text in the code block */
  text-overflow: ellipsis;
  -webkit-line-break: after-white-space;
  line-break: after-white-space;
}

/* Flex layout improvements */
.min-w-0 {
  min-width: 0;
}

.flex-shrink-0 {
  flex-shrink: 0;
}

.flex-nowrap {
  flex-wrap: nowrap;
}

/* Modal height control and scrolling */
.modal-dialog {
  max-height: 90vh;
  margin: 5vh auto;
}

.modal-content {
  max-height: 90vh;
  display: flex;
  flex-direction: column;
}

.modal-body-scrollable {
  overflow-y: auto;
  max-height: calc(90vh - 140px);
  /* Account for header and footer */
  flex: 1;
  min-height: 0;
}

.modal-header {
  flex-shrink: 0;
}

.modal-footer {
  flex-shrink: 0;
}

/* Commands sections with controlled scrolling */
.commands-section {
  max-height: 400px;
  overflow-y: auto;
  padding-right: 8px;
}

.commands-section::-webkit-scrollbar {
  width: 6px;
}

.commands-section::-webkit-scrollbar-track {
  background: #f1f1f1;
  border-radius: 10px;
}

.commands-section::-webkit-scrollbar-thumb {
  background: #c1c1c1;
  border-radius: 10px;
}

.commands-section::-webkit-scrollbar-thumb:hover {
  background: #a8a8a8;
}

/* Action columns with enhanced styling */
/* Ensure action-columns and action-col stretch to fill available height */
.action-columns {
  min-width: 0;
  width: 100%;
  min-height: 200px;
  max-height: 600px;
  height: 100%;
  display: flex;
  flex-direction: row;
  align-items: stretch;
  overflow-x: auto;
  overflow-y: auto;
  border-radius: 0 0 12px 12px;
}

.action-col {
  min-width: 350px;
  max-width: 100%;
  flex: 1 1 0;
  display: flex;
  flex-direction: column;
  position: relative;
  height: 100%;
}

.action-col-startup {
  background: linear-gradient(135deg, #f8fffe 0%, #f0fdf4 100%);
  background-attachment: local;
}

.action-col-cleanup {
  background: linear-gradient(135deg, #fffbf0 0%, #fef3c7 100%);
  background-attachment: local;
}

/* Enhanced form check styling */
.form-check-input {
  transition: all 0.2s ease;
}

.form-check-input:checked {
  background-color: #0d6efd;
  border-color: #0d6efd;
}

/* Gradient text effects - only apply to specific classes to avoid conflicts */
.gradient-text-primary {
  background: linear-gradient(135deg, #0d6efd, #6610f2);
  -webkit-background-clip: text;
  -webkit-text-fill-color: transparent;
  background-clip: text;
}

/* Improved spacing and typography */
.fw-bold {
  font-weight: 600 !important;
}

.fw-semibold {
  font-weight: 500 !important;
}

.fw-medium {
  font-weight: 500 !important;
}
</style>
