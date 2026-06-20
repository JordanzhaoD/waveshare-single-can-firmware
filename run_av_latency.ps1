
$ErrorActionPreference='SilentlyContinue'
$dnsServer='100.100.1.1'
$items=@(
  @{cat='music'; service='QQ Music'; domain='y.qq.com'},
  @{cat='music'; service='QQ Music API'; domain='c.y.qq.com'},
  @{cat='music'; service='NetEase Music'; domain='music.163.com'},
  @{cat='music'; service='NetEase API'; domain='interface.music.163.com'},
  @{cat='music'; service='Apple Music'; domain='music.apple.com'},
  @{cat='music'; service='Apple CDN'; domain='is1-ssl.mzstatic.com'},
  @{cat='music'; service='Kugou'; domain='www.kugou.com'},
  @{cat='music'; service='Spotify'; domain='open.spotify.com'},
  @{cat='music'; service='Amazon Music'; domain='music.amazon.com'},
  @{cat='music'; service='Tidal'; domain='tidal.com'},
  @{cat='music'; service='TuneIn'; domain='tunein.com'},
  @{cat='music'; service='Audible'; domain='www.audible.com'},
  @{cat='music'; service='Apple Podcasts'; domain='podcasts.apple.com'},
  @{cat='music'; service='LiveOne'; domain='www.liveone.com'},
  @{cat='music'; service='YouTube Music'; domain='music.youtube.com'},
  @{cat='video'; service='Douyin'; domain='www.douyin.com'},
  @{cat='video'; service='Xigua'; domain='www.ixigua.com'},
  @{cat='video'; service='Toutiao'; domain='www.toutiao.com'},
  @{cat='video'; service='iQiyi'; domain='www.iqiyi.com'},
  @{cat='video'; service='Bilibili'; domain='www.bilibili.com'},
  @{cat='video'; service='Mango TV'; domain='www.mgtv.com'},
  @{cat='video'; service='Netflix'; domain='www.netflix.com'},
  @{cat='video'; service='Tencent Video'; domain='v.qq.com'},
  @{cat='video'; service='Youku'; domain='www.youku.com'}
)
function Test-One($it){
  $domain=$it.domain
  $dnsMs=$null; $dnsOk=$false; $dnsErr=''
  try{
    $sw=[Diagnostics.Stopwatch]::StartNew()
    $ans=Resolve-DnsName -Server $dnsServer -Name $domain -Type A -DnsOnly -ErrorAction Stop
    $sw.Stop(); $dnsMs=[int]$sw.ElapsedMilliseconds; $dnsOk=$true
  }catch{ $dnsErr=$_.Exception.Message }
  $url="https://$domain/"
  $curlArgs=@('-I','-L','--connect-timeout','5','--max-time','8','-o','NUL','-sS','-w','%{http_code} %{time_namelookup} %{time_connect} %{time_appconnect} %{time_starttransfer} %{time_total}', $url)
  $out=& curl.exe @curlArgs 2>&1
  $httpOk=$false; $code='ERR'; $nameMs=$connMs=$tlsMs=$ttfbMs=$totalMs=$null; $err=''
  if($LASTEXITCODE -eq 0 -and $out -match '^(\d{3})\s+([0-9.]+)\s+([0-9.]+)\s+([0-9.]+)\s+([0-9.]+)\s+([0-9.]+)'){
    $code=$matches[1]
    $nameMs=[math]::Round([double]$matches[2]*1000)
    $connMs=[math]::Round([double]$matches[3]*1000)
    $tlsMs=[math]::Round([double]$matches[4]*1000)
    $ttfbMs=[math]::Round([double]$matches[5]*1000)
    $totalMs=[math]::Round([double]$matches[6]*1000)
    $httpOk=$true
  } else { $err=($out -join ' ') }
  [pscustomobject]@{Cat=$it.cat; Service=$it.service; Domain=$domain; DnsMs=$dnsMs; DnsOk=$dnsOk; Http=$code; NameMs=$nameMs; ConnMs=$connMs; TlsMs=$tlsMs; TtfbMs=$ttfbMs; TotalMs=$totalMs; HttpOk=$httpOk; Error=($dnsErr+' '+$err).Trim()}
}
$results=@()
foreach($it in $items){ $r=Test-One $it; $results+=$r; Write-Host (($r|ConvertTo-Json -Compress)) }
$path='wifi_av_latency_results_ali.csv'
$results | Export-Csv -NoTypeInformation -Encoding UTF8 $path
Write-Host "CSV=$path"
