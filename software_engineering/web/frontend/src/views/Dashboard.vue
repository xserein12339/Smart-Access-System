<template>
  <div class="page-container">
    <el-row :gutter="12">
      <el-col :xs="24" :sm="12" :md="8" v-for="d in devices" :key="d.device_id">
        <el-card shadow="hover" class="dev-card">
          <div class="dev-head">
            <span class="dev-id">{{ d.device_id }}</span>
            <el-tag :type="d.online ? 'success' : 'info'" size="small">
              {{ d.online ? '在线' : '离线' }}
            </el-tag>
          </div>
          <div class="dev-info">
            <div>IP：{{ d.ip || '-' }}</div>
            <div>固件：{{ d.fw || '-' }}</div>
            <div>人员库：{{ d.db_count }} 人</div>
            <div>运行：{{ d.uptime_s }}s</div>
            <div class="muted">最后上报：{{ d.last_seen || '-' }}</div>
          </div>
        </el-card>
      </el-col>
    </el-row>
    <el-empty v-if="!devices.length" description="暂无设备上报，请确认设备已连 broker" />

    <el-card v-if="recent.length" class="recent" shadow="never">
      <template #header>最近通行</template>
      <el-table :data="recent" size="small" :max-height="300">
        <el-table-column prop="device_id" label="设备" min-width="120" />
        <el-table-column prop="person_id" label="人员ID" width="90" />
        <el-table-column label="结果" width="80">
          <template #default="{ row }">
            <el-tag :type="resultType(row.result)" size="small">{{ row.result_txt }}</el-tag>
          </template>
        </el-table-column>
        <el-table-column prop="method_txt" label="方式" width="80" />
        <el-table-column prop="received_at" label="时间" min-width="160" />
      </el-table>
    </el-card>
  </div>
</template>

<script setup lang="ts">
import { ref, onMounted, onUnmounted } from 'vue'
import api from '../api'
import { useRealtimeStore, type RtMsg } from '../store/realtime'

const devices = ref<any[]>([])
const recent = ref<any[]>([])
const rt = useRealtimeStore()

async function loadDevices() {
  try {
    const { data } = await api.get('/devices')
    devices.value = data
  } catch (e) { /* ignore */ }
}
async function loadRecords() {
  try {
    const { data } = await api.get('/records', { params: { limit: 10 } })
    recent.value = data.items
  } catch (e) { /* ignore */ }
}

function resultType(r: number) {
  return r === 0 ? 'success' : r === 1 ? 'warning' : 'danger'
}

function onMsg(m: RtMsg) {
  if (m.kind === 'online' || m.kind === 'offline') {
    loadDevices()
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
.dev-card { margin-bottom: 12px; }
.dev-head { display: flex; justify-content: space-between; align-items: center; margin-bottom: 8px; }
.dev-id { font-weight: 600; }
.dev-info { font-size: 14px; color: #606266; line-height: 1.8; }
.muted { color: #909399; font-size: 12px; }
.recent { margin-top: 12px; }
</style>
