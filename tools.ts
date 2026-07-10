import { existsSync } from 'fs'
import { cpus } from 'os'
import { join } from 'path'
import { spawn } from 'child_process'

const executableName = (name: string) => process.platform === 'win32' ? `${name}.exe` : name

const findTool = (environmentVariable: string, folder: string, name: string) => {
  const override = process.env[environmentVariable]
  if (override) {
    return override
  }

  const executable = executableName(name)
  const candidates = process.platform === 'win32'
    ? [
        join(import.meta.dirname, folder, 'build', 'Release', executable),
        join(import.meta.dirname, folder, 'build', executable)
      ]
    : [join(import.meta.dirname, folder, 'build', executable)]

  const tool = candidates.find(existsSync)
  if (!tool) {
    throw new Error(
      `${name} is not built. Run ${join(import.meta.dirname, folder, process.platform === 'win32' ? 'build.ps1' : 'build.sh')}`
    )
  }
  return tool
}

export const parallelExecute = async (tasks: (() => Promise<void>)[], multiplier = 2) => {
  if (tasks.length === 0) {
    return
  }

  const workerCount = Math.min(tasks.length, cpus().length * multiplier)
  const workers = [] as Promise<void>[]
  let nextTask = 0
  let taskDone = 0
  let nextProgress = 10

  for (let i = 0; i < workerCount; i++) {
    workers.push((async () => {
      while (nextTask < tasks.length) {
        const task = tasks[nextTask++]
        await task()
        taskDone++
        const progress = Math.floor(taskDone / tasks.length * 100)
        if (progress >= nextProgress || taskDone === tasks.length) {
          console.log(`${progress}%`)
          nextProgress += 10
        }
      }
    })())
  }
  await Promise.all(workers)
}

const execute = (command: string, args: string[]) => new Promise<void>((resolve, reject) => {
  const child = spawn(command, args)
  let stdout = ''
  let stderr = ''
  let settled = false

  child.stdout.on('data', data => {
    const text = data.toString()
    stdout += text
    process.stdout.write(text)
  })
  child.stderr.on('data', data => {
    const text = data.toString()
    stderr += text
    process.stderr.write(text)
  })
  child.on('error', error => {
    if (!settled) {
      settled = true
      reject(error)
    }
  })
  child.on('close', code => {
    if (settled) {
      return
    }
    settled = true
    if (code === 0) {
      resolve()
    } else {
      reject(new Error(`${command} exited with code ${code}\n${stdout}\n${stderr}`))
    }
  })
})

export const unpackPCK = async (source: string, output: string) => {
  const tool = findTool('HOYO_PCK_EXTRACT', 'quickpck', 'hoyo-pck-extract')
  await execute(tool, [source, output])
}

export const convertWwise = async (source: string, output: string) => {
  const tool = findTool('HOYO_AUDIO_CONVERT', 'wwise', 'hoyo-audio-convert')
  await execute(tool, [source, output])
}

