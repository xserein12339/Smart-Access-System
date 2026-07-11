<template>
  <div class="page-container">
    <el-card shadow="never">
      <el-form :inline="true">
        <el-form-item label="设备">
          <el-select v-model="filter.device_id" clearable placeholder="全部" style="width:180px" @change="load">
            <el-option v-for="d in devices" :key="d.device_id" :label="d.device_id" :value="d.device_id" />
          </el-select>
        </el-form-item>
        <el-form-item>
          <el-button @click="load">刷新</el-button>
        </el-form-item>
      </el-form>

      <el-table :data="items" size="small" :max-height="height" v-loading="loading">
        <el-table-column prop="device_id" label="设备" min-width="120" />
        <el-table-column prop="person_id" label="人员ID" width="90" />
        <el-table-column label="结果" width="80">
          <template #default="{ row }">
            <el-tag :type="row.result === 0 ? 'success' : row.result === 1 ? 'warning' : 'danger'" size="small">
              {{ row.result_txt }}
            </el-tag>
          </template>
        </el-table-column>
        <el-table-column prop="method_txt" label="方式" width="80" />
        <el-table-column prop="received_at" label="接收时间" min-width="180" />
      </el-table>

      <div class="pager">
        <el-button size="small" :disabled="!offset" @click="prev">上一页</el-button>
        <el-button size="small" :disabled="items.length < limit" @click="next">下一页</el-button>
        <span class="muted">每页 {{ limit }} 条</span>
      </div>
    </el-card>
  </div>
</template>

<script setup lang="ts">
import { ref, reactive, onMounted, onUnmounted } from 'vue'
import api from '../api'
import { useRealtimeStore, type RtMsg } from '../store/realtime'

const devices = ref<any[]>([])
const items = ref<any[]>([])
const filter = reactive({ device_id: '' })
const limit = 50
const offset = ref(0)
const loading = ref(false)
const height = ref(window.innerHeight - 240)
const rt = useRealtimeStore()

async function loadDevices() {
  try { const { data } = await api.get('/devices'); devices.value = data } catch (e) { /* */ }
}
async function load() {
  loading.value = true
  try {
    const { data } = await api.get('/records', {
      params: { device_id: filter.device_id || undefined, limit, offset: offset.value },
    })
    items.value = data.items
  } finally { loading.value = false }
}
function prev() { offset.value = Math.max(0, offset.value - limit); load() }
function next() { offset.value += limit; load() }

function onMsg(m: RtMsg) { if (m.kind === 'record') load() }
function onResize() { height.value = window.innerHeight - 240 }

onMounted(() => {
  loadDevices(); load()
  rt.on(onMsg)
  window.addEventListener('resize', onResize)
})
onUnmounted(() => { rt.off(onMsg); window.removeEventListener('resize', onResize) })
</script>

<style scoped>
.pager { margin-top: 12px; display: flex; align-items: center; gap: 8px; }
.muted { color: #909399; font-size: 12px; }
</style>
