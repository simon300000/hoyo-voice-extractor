import { readdir, stat, mkdir, readFile, writeFile } from 'fs/promises'
import { join, extname, dirname } from 'path'

import { fnv1 } from './fnvjs/src/index.js'

import { unpackPCK, convertWwise } from './tools.js'

export const checkResultFolder = async (folder: string) => {
  await mkdir(folder, { recursive: true })
  const files = await readdir(folder)
  const generatedFolders = ['wav', 'wem', 'wav-pass1', 'wav-pass2', 'wem-pass1', 'wem-bnk-pass2']
  const badFiles = generatedFolders.filter(f => files.includes(f))
  if (badFiles.length) {
    throw new Error(`Result folder already exists. Remove the following paths from ${folder}:\n${badFiles.join('\n')}`)
  }
}

export const findFiles = async (folder: string, ext: string) => {
  const files = await readdir(folder)
  const result = [] as string[]
  const resultPromise = [] as Promise<string[]>[]
  for (const file of files) {
    const filePath = join(folder, file)
    const fileStat = await stat(filePath)
    if (fileStat.isDirectory()) {
      resultPromise.push(findFiles(filePath, ext))
    } else {
      if (extname(file) === ext) {
        result.push(filePath)
      }
    }
  }
  return [...result, ...(await Promise.all(resultPromise)).flat()]
}

export const findWAV = async (path: string) => findFiles(path, '.wav')
export const findJSON = async (path: string) => findFiles(path, '.json')

export const readJSON = async <T>(path: string) => JSON.parse(await readFile(path, 'utf8')) as T

export const readJSONWithStringHashes = async <T>(path: string) => {
  const json = await readFile(path, 'utf8')
  return JSON.parse(json.replace(/("Hash"\s*:\s*)(-?\d+)/g, '$1"$2"')) as T
}

export const readJSONs = async <T>(paths: string[]) => {
  const result = [] as T[]
  while (paths.length) {
    const batch = paths.splice(0, 256)
    result.push(...await Promise.all(batch.map(readJSON<T>)))
  }
  return result
}

export const createFolders = async (files: string[], existing = new Set<string>()) => {
  const folders = new Set<string>(existing)
  for (const file of files) {
    const folder = dirname(file)
    if (!folders.has(folder)) {
      await mkdir(folder, { recursive: true })
      folders.add(folder)
    }
  }
}

export const unpackPCKs = async (source: string, wemFolder: string, wavFolder: string) => {
  await mkdir(wemFolder, { recursive: true })
  await mkdir(wavFolder, { recursive: true })
  console.log('Extracting PCK archives...')
  await unpackPCK(source, wemFolder)
  const wems = await findFiles(wemFolder, '.wem')
  const bnks = await findFiles(wemFolder, '.bnk')
  console.log(`Found ${wems.length} wem and ${bnks.length} bnk files.`)
  console.log('Converting Wwise audio...')
  await convertWwise(wemFolder, wavFolder)
}

export const unpackBNKs = async (source: string, wavFolder: string) => {
  const bnks = await findFiles(source, '.bnk')
  console.log(`Found ${bnks.length} bnk files.`)
  console.log('Converting Wwise audio...')
  await convertWwise(source, wavFolder)
}

export const encodeFNV64 = (input: string) => fnv1(input.toLowerCase(), 64).toString(16).padStart(16, '0')

export const readTextMap = async <TextMapMap extends Record<string, string | readonly string[]>>(dataPath: string, textMapMap: TextMapMap) => {
  return Object.fromEntries(await Promise.all(Object.entries(textMapMap).map(async ([language, value]) => {
    const filenames = Array.isArray(value) ? value : [value]
    const maps = await Promise.all(filenames.map(filename => readJSON<TextMap>(join(dataPath, 'TextMap', filename))))
    return [language, Object.assign({}, ...maps)]
  }))) as Record<keyof TextMapMap, TextMap>
}

export const updateStats = async <Voice extends VoiceBase>(voiceMap: Record<string, Voice>, readme: string) => {
  const currentDate = new Date().toISOString().replace(/T.*/, '')

  let noSpeaker = 0
  let noText = 0
  let noFileName = 0

  for (const voice of Object.values(voiceMap)) {
    if (!voice.speaker) {
      noSpeaker++
    }
    if (!voice.transcription) {
      noText++
    }
    if (!voice.inGameFilename) {
      noFileName++
    }
  }

  const wavCount = Object.keys(voiceMap).length
  const percentage = (count: number) => wavCount === 0 ? 0 : Math.round(count / wavCount * 100)

  const stats = `<!-- STATS -->
Last update at \`${currentDate}\`

\`${wavCount}\` wavs

\`${noSpeaker}\` without speaker (${percentage(noSpeaker)}%)

\`${noText}\` without transcription (${percentage(noText)}%)

\`${noFileName}\` without inGameFilename (${percentage(noFileName)}%)
<!-- STATS_END -->`

  console.log(stats)

  const originalReadme = await readFile(readme, 'utf-8')
  const updatedReadme = originalReadme.replace(/<!-- STATS -->[\s\S]*<!-- STATS_END -->/, stats)
  await writeFile(readme, updatedReadme)
}

export const copyReadme = async (source: string, destination: string, huggingfaceMetadata: string) => {
  const readme = await readFile(source, 'utf-8')
  await writeFile(destination, `${huggingfaceMetadata}\n\n${readme}`)
}

type TextMap = Record<string, string>

type VoiceBase = {
  inGameFilename: string
  language: string
  transcription: string
  speaker: string
}
