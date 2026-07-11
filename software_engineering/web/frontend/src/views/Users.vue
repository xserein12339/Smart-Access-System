<template>
  <div class="page-container">
    <el-card shadow="never">
      <el-form :inline="true" :model="form">
        <el-form-item label="设备">
          <el-select v-model="form.device_id" placeholder="选择设备" style="width:180px">
            <el-option v-for="d in devices" :key="d.device_id" :label="d.device_id" :value="d.device_id" />
          </el-select>
        </el-form-item>
        <el-form-item label="人员ID">
          <el-input-number v-model="form.person_id" :min="1" controls-position="right" style="width:120px" />
        </el-form-item>
        <el-form-item label="姓名">
          <el-input v-model="form.name" placeholder="姓名" style="width:140px" />
        </el-form-item>
        <el-form-item>
          <el-button type="primary" :loading="adding" :disabled="!form.device_id" @click="onAdd">新增人员</el-button>
          <el-button type="danger" :loading="deling" :disabled="!form.device_id" @click="onDel">删除人员</el-button>
        </el-form-item>
      </el-form>

      <el-alert v-if="lastAck" :title="ackText" :type="lastAck.code === 0 ? 'success' : 'error'" :closable="false" />
    </el-card>

    <el-card class="hint" shadow="never">
      <template #header>说明</template>
      <p class="muted">
        本期人员管理通过 MQTT 命令下发到设备，由设备 svc_perm_manager 写入 db_store 人员库。
        新增人员为占位记录（零特征），真实人脸特征录入属 Phase2。
        设备当前人员数见仪表盘「人员库」字段。
      </p>
    </el-card>
  </div>
</template>

<script setup lang="ts">
import { ref, reactive, computed, onMounted } from 'vue'
import { ElMessage } from 'element-plus'
import api from '../api'

const devices = ref<any[]>([])
const form = reactive({ device_id: '', person_id: 1, name: '' })
const adding = ref(false)
const deling = ref(false)
const lastAck = ref<any>(null)

const ackText = computed(() => {
  if (!lastAck.value) return ''
  const a = lastAck.value
  return a.code === 0 ? `操作成功 (msg_id=${a.msg_id})`
       : a.code === -2 ? '超时，未收到设备回执'
       : `失败：${a.msg} (code=${a.code})`
})

async function loadDevices() {
  try {
    const { data } = await api.get('/devices')
    devices.value = data
    if (!form.device_id && data.length) form.device_id = data[0].device_id
  } catch (e) { /* ignore */ }
}

async function onAdd() {
  if (!form.name) { ElMessage.warning('请输入姓名'); return }
  adding.value = true; lastAck.value = null
  try {
    const { data } = await api.post('/persons', {
      device_id: form.device_id, person_id: form.person_id, name: form.name,
    })
    lastAck.value = data
    data.code === 0 ? ElMessage.success('新增成功') : ElMessage.error(data.msg)
  } catch (e: any) {
    ElMessage.error(e.response?.data?.detail || '请求失败')
  } finally { adding.value = false }
}

async function onDel() {
  deling.value = true; lastAck.value = null
  try {
    const { data } = await api.delete('/persons', {
      data: { device_id: form.device_id, person_id: form.person_id },
    })
    lastAck.value = data
    data.code === 0 ? ElMessage.success('删除成功') : ElMessage.error(data.msg)
  } catch (e: any) {
    ElMessage.error(e.response?.data?.detail || '请求失败')
  } finally { deling.value = false }
}

onMounted(loadDevices)
</script>

<style scoped>
.hint { margin-top: 12px; }
.muted { color: #909399; font-size: 13px; line-height: 1.8; margin: 0; }
</style>
