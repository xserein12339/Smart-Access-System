<template>
  <div class="page-container">
    <!-- 固件上传 -->
    <el-card shadow="never" class="mb">
      <template #header>固件上传</template>
      <el-upload
        :auto-upload="false"
        :on-change="onFileChange"
        :limit="1"
        accept=".bin"
        drag
      >
        <el-icon size="40"><UploadFilled /></el-icon>
        <div>拖拽 .bin 文件到这里或点击上传</div>
      </el-upload>
      <div v-if="fileInfo" class="file-info">
        <el-tag>文件: {{ fileInfo.name }}</el-tag>
        <el-tag type="info">大小: {{ (fileInfo.size/1024).toFixed(1) }} KB</el-tag>
      </div>
      <el-button type="primary" :loading="uploading" :disabled="!fileInfo"
                 @click="onUpload" style="margin-top:8px">上传到服务器</el-button>
    </el-card>

    <!-- 固件列表 -->
    <el-card shadow="never" class="mb">
      <template #header>固件版本</template>
      <el-table :data="firmwares" size="small" :max-height="300" v-loading="loadingFw">
        <el-table-column prop="version" label="版本" width="100" />
        <el-table-column prop="filename" label="文件名" min-width="160" />
        <el-table-column label="大小" width="100">
          <template #default="{ row }">{{ (row.size/1024).toFixed(1) }} KB</template>
        </el-table-column>
        <el-table-column prop="sha256" label="SHA256" min-width="200" show-overflow-tooltip />
        <el-table-column prop="uploaded_at" label="上传时间" min-width="160" />
        <el-table-column label="操作" width="80">
          <template #default="{ row }">
            <el-button size="small" type="danger" @click="onDelete(row.id)">删除</el-button>
          </template>
        </el-table-column>
      </el-table>
      <el-empty v-if="!firmwares.length && !loadingFw" description="暂无固件" />
    </el-card>

    <!-- OTA 下发 -->
    <el-card shadow="never" v-if="firmwares.length">
      <template #header>OTA 升级</template>
      <el-form :inline="true">
        <el-form-item label="设备">
          <el-select v-model="ota.device_id" placeholder="选择设备" style="width:200px">
            <el-option v-for="d in devices" :key="d.device_id" :label="d.device_id" :value="d.device_id" />
          </el-select>
        </el-form-item>
        <el-form-item label="固件版本">
          <el-select v-model="ota.firmware_id" placeholder="选择版本" style="width:160px">
            <el-option v-for="fw in firmwares" :key="fw.id" :label="fw.version" :value="fw.id" />
          </el-select>
        </el-form-item>
        <el-form-item>
          <el-button type="success" :loading="startingOta"
                     :disabled="!ota.device_id || !ota.firmware_id"
                     @click="onStartOta">开始升级</el-button>
        </el-form-item>
      </el-form>

      <!-- 实时进度 -->
      <div v-if="otaProgress" class="progress-area">
        <el-progress :percentage="otaProgress" :status="otaStatus" />
        <el-tag :type="otaStatus === 'success' ? 'success' : otaStatus === 'exception' ? 'danger' : ''"
                size="small">{{ otaMsg }}</el-tag>
      </div>
    </el-card>
  </div>
</template>

<script setup lang="ts">
import { ref, reactive, onMounted, onUnmounted } from 'vue'
import { ElMessage, ElMessageBox } from 'element-plus'
import api from '../api'
import { useRealtimeStore, type RtMsg } from '../store/realtime'

const fileInfo = ref<any>(null)
const uploading = ref(false)
const firmwares = ref<any[]>([])
const loadingFw = ref(false)
const devices = ref<any[]>([])
const startingOta = ref(false)
const ota = reactive({ device_id: '', firmware_id: 0 })
const otaProgress = ref(0)
const otaStatus = ref<'success' | 'exception' | ''>('')
const otaMsg = ref('')
const rt = useRealtimeStore()

function onFileChange(_file: any, fileList: any) {
  const f = fileList[0]
  fileInfo.value = f ? { name: f.name, size: f.size, raw: f.raw } : null
}

async function onUpload() {
  const raw = fileInfo.value?.raw
  if (!raw) return
  uploading.value = true
  try {
    const fd = new FormData()
    fd.append('file', raw)
    const { data } = await api.post('/firmware/upload', fd)
    ElMessage.success(`上传成功: ${data.version}`)
    fileInfo.value = null
    loadFirmwares()
  } catch (e: any) {
    ElMessage.error(e.response?.data?.detail || '上传失败')
  } finally { uploading.value = false }
}

async function loadFirmwares() {
  loadingFw.value = true
  try { const { data } = await api.get('/firmware'); firmwares.value = data } catch (e) { /* */ }
  finally { loadingFw.value = false }
}

async function onDelete(id: number) {
  try {
    await ElMessageBox.confirm('确认删除该固件版本？')
    await api.delete(`/firmware/${id}`)
    loadFirmwares()
  } catch (e) { /* canceled */ }
}

async function loadDevices() {
  try { const { data } = await api.get('/devices'); devices.value = data } catch (e) { /* */ }
}

async function onStartOta() {
  startingOta.value = true
  otaProgress.value = 0; otaStatus.value = ''; otaMsg.value = ''
  try {
    const { data } = await api.post('/ota/start', ota)
    ElMessage.info(`OTA 命令已下发, msg_id=${data.msg_id}`)
  } catch (e: any) {
    ElMessage.error(e.response?.data?.detail || '下发失败')
  } finally { startingOta.value = false }
}

function onRtMsg(m: RtMsg) {
  if (m.kind === 'ota_progress') {
    otaProgress.value = m.percent || 0
    otaMsg.value = `${m.percent}%`
  } else if (m.kind === 'ota_result') {
    if (m.code === 0) {
      otaStatus.value = 'success'
      otaMsg.value = m.msg || '升级成功，设备重启中'
    } else {
      otaStatus.value = 'exception'
      otaMsg.value = m.msg || `失败 (code=${m.code})`
    }
  }
}

onMounted(() => { loadFirmwares(); loadDevices(); rt.on(onRtMsg) })
onUnmounted(() => rt.off(onRtMsg))
</script>

<style scoped>
.mb { margin-bottom: 12px; }
.file-info { display: flex; gap: 8px; margin-top: 8px; }
.progress-area { display: flex; align-items: center; gap: 12px; margin-top: 12px; }
</style>
