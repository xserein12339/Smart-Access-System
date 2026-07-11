<template>
  <div class="page-container door-page">
    <el-card shadow="never">
      <el-form :inline="true" :model="form">
        <el-form-item label="设备">
          <el-select v-model="form.device_id" placeholder="选择设备" style="width:200px">
            <el-option v-for="d in devices" :key="d.device_id"
                       :label="d.device_id + (d.online ? '' : '(离线)')"
                       :value="d.device_id" />
          </el-select>
        </el-form-item>
      </el-form>

      <div class="btn-wrap">
        <el-button class="open-door-btn" type="primary" :loading="opening"
                   :disabled="!form.device_id" @click="onOpen">
          <el-icon size="40"><Unlock /></el-icon>
          <div>{{ opening ? '开门中…' : '远程开门' }}</div>
        </el-button>
      </div>

      <el-alert v-if="lastAck" :title="ackTitle" :type="ackType" :closable="false" class="ack" />
    </el-card>

    <el-card class="recent" shadow="never">
      <template #header>最近开门记录</template>
      <el-table :data="recent" size="small" :max-height="300">
        <el-table-column prop="device_id" label="设备" min-width="120" />
        <el-table-column label="结果" width="80">
          <template #default="{ row }">
            <el-tag :type="row.result === 0 ? 'success' : 'danger'" size="small">{{ row.result_txt }}</el-tag>
          </template>
        </el-table-column>
        <el-table-column prop="method_txt" label="方式" width="80" />
        <el-table-column prop="received_at" label="时间" min-width="160" />
      </el-table>
    </el-card>
  </div>
</template>

<script setup lang="ts">
import { ref, reactive, computed, onMounted, onUnmounted } from 'vue'
import { ElMessage } from 'element-plus'
import api from '../api'
import { useRealtimeStore, type RtMsg } from '../store/realtime'

const devices = ref<any[]>([])
const form = reactive({ device_id: '' })
const opening = ref(false)
const lastAck = ref<any>(null)
const recent = ref<any[]>([])
const rt = useRealtimeStore()

const ackTitle = computed(() => {
  if (!lastAck.value) return ''
  const c = lastAck.value.code
  return c === 0 ? `开门成功 (msg_id=${lastAck.value.msg_id})`
       : c === -2 ? '命令超时，未收到设备回执'
       : `失败：${lastAck.value.msg} (code=${c})`
})
const ackType = computed(() => {
  const c = lastAck.value?.code
  return c === 0 ? 'success' : 'error'
})

async function loadDevices() {
  try {
    const { data } = await api.get('/devices')
    devices.value = data
    if (!form.device_id && data.length) form.device_id = data[0].device_id
  } catch (e) { /* ignore */ }
}
async function loadRecords() {
  try {
    const { data } = await api.get('/records', { params: { limit: 20, device_id: form.device_id || undefined } })
    recent.value = data.items
  } catch (e) { /* ignore */ }
}

async function onOpen() {
  if (!form.device_id) {
    ElMessage.warning('请先选择设备')
    return
  }
  opening.value = true
  lastAck.value = null
  try {
    const { data } = await api.post('/door/open', { device_id: form.device_id })
    lastAck.value = data
    if (data.code === 0) ElMessage.success('开门成功')
    else ElMessage.error(data.msg || '开门失败')
    loadRecords()
  } catch (e: any) {
    ElMessage.error(e.response?.data?.detail || '请求失败')
    lastAck.value = { code: -1, msg: 'http_error' }
  } finally {
    opening.value = false
  }
}

function onMsg(m: RtMsg) {
  if (m.kind === 'cmd_ack' && m.device_id === form.device_id) {
    lastAck.value = { msg_id: m.msg_id, code: m.code, msg: m.msg }
  } else if (m.kind === 'record') {
    loadRecords()
  }
}

onMounted(() => {
  loadDevices()
  loadRecords()
  rt.on(onMsg)
})
onUnmounted(() => rt.off(onMsg))
</script>

<style scoped>
.door-page { max-width: 720px; margin: 0 auto; }
.btn-wrap { display: flex; justify-content: center; padding: 24px 0; }
.open-door-btn { display: flex; flex-direction: column; align-items: center; gap: 8px; }
.ack { margin-top: 8px; }
.recent { margin-top: 12px; }
</style>
